/* Bank-1 wrapper: same call shape as smooth_mix; Bungee DSP in bank 2 (XIP).
 * Returns 0 (hook_lane then plays stock grains) when bank 2 is not paired or the leaf
 * could not start. */
#include <stdint.h>
#include "bank2_syms.h"

extern int b2_live(void);

#define BG_MAGIC   0x424E4745u   /* 'BNGE' in the voice slot header */
#define SM_BASE    0x38007000u   /* shared voice: [1] owner, [5] reset_req, [6] block_done */
#define BG_ARENA   0x38005200u   /* {magic, base, len}: SDRAM arena, cleared by cpu_boot */
#define BG_ARENA_MAGIC 0x42474152u /* 'BGAR' */
#define BG_ARENA_LEN   (2u << 20)

typedef void (*bg_read_fn)(void *, int, int, float *, float *);
typedef int (*bg_init_fn)(void *, uint32_t);
typedef void (*bg_restart_fn)(float);
typedef void (*bg_mix_fn)(bg_read_fn, void *, float *, float *, int, float);
typedef void *(*bump_fn)(uint32_t);

typedef int (*pcm_read_fn)(void *, float *, int, int, int);
typedef int (*pcm_len_fn)(void *);
typedef int (*pcm_st_fn)(void *);
typedef void *(*buf_slot_fn)(void *, int);
typedef void (*buf_clear_fn)(void *);
typedef void (*buf_setlen_fn)(void *, int);
typedef int (*buf_len_fn)(void *);
typedef void (*buf_planes_fn)(void *, float **, float **);
typedef void (*buf_env_fn)(void *, void *);
typedef void (*buf_mix_fn)(void *, void *);

typedef struct {
    void *sample;
    int length;
    int stereo;
} BgClip;

/* Frames before the clip start are silence (a launch must not pull in the loop tail);
 * frames past the end wrap, since the clip loops. Contiguous runs go to the stock reader
 * in one call each. */
static void read_chan(BgClip *c, int pos, int n, float *dest, int chan)
{
    int i = 0;
    while (i < n) {
        int p = pos + i, k;
        if (p < 0) {
            k = -p < n - i ? -p : n - i;
            while (k--)
                dest[i++] = 0.f;
            continue;
        }
        p %= c->length;
        k = c->length - p;
        if (k > n - i)
            k = n - i;
        ((pcm_read_fn)0x08063635u)(c->sample, dest + i, k, p, chan);
        i += k;
    }
}

static void bg_read(void *ctx, int pos, int n, float *l, float *r)
{
    BgClip *c = (BgClip *)ctx;
    int i;
    read_chan(c, pos, n, l, 0);
    if (c->stereo)
        read_chan(c, pos, n, r, 1);
    else
        for (i = 0; i < n; i++)
            r[i] = l[i];
}

static void *arena(void)
{
    volatile uint32_t *a = (volatile uint32_t *)BG_ARENA;
    if (a[0] != BG_ARENA_MAGIC) {
        void *p = ((bump_fn)(0x0804436Cu | 1u))(BG_ARENA_LEN);  /* never freed: once per boot */
        if (!p)
            return 0;
        a[1] = (uint32_t)p;
        a[2] = BG_ARENA_LEN;
        a[0] = BG_ARENA_MAGIC;
    }
    return (void *)a[1];
}

int bg_mix_fw(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    volatile uint32_t *slot = (volatile uint32_t *)SM_BASE;
    BgClip c;
    void *scratch, *mem;
    float *dl, *dr;
    int n, reset;
    float ratio;
    unsigned char a0, a1;

    if (!b2_live())
        return 0;
    if (clip == 0 || lane == 0 || mix == 0 || ctx == 0)
        return 0;
    c.sample = *(void **)(clip + 0x24);
    if (!c.sample)
        return 0;
    c.length = ((pcm_len_fn)0x080636f1u)(c.sample);
    c.stereo = ((pcm_st_fn)0x08063791u)(c.sample) ? 1 : 0;
    if (c.length <= 0)
        return 0;
    scratch = ((buf_slot_fn)0x0805f3a5u)((void *)ctx, 1);
    if (!scratch)
        return 0;
    ((buf_clear_fn)0x0804d8ddu)(scratch);
    n = ((buf_len_fn)0x0804d8e9u)((void *)mix);
    if (n <= 0)
        return 0;
    if (n > 2048)
        n = 2048;
    ((buf_setlen_fn)0x0804d8d1u)(scratch, n);
    dl = 0;
    dr = 0;
    ((buf_planes_fn)0x0804d9c1u)(scratch, &dl, &dr);
    if (!dl)
        return 0;

    ratio = *(float *)(clip + 0xBB4);
    if (!(ratio > 1e-3f))
        ratio = 1.f;
    reset = (int)slot[5];
    slot[5] = 0;
    if (slot[0] != BG_MAGIC) {
        /* Another Warp mode had the slot (and wrote over the leaf's .data/.bss). */
        mem = arena();
        if (!mem || ((bg_init_fn)BG_INIT_ADDR)(mem, BG_ARENA_LEN) != 0)
            return 0;
        slot[0] = BG_MAGIC;
        slot[9] = 0;
        reset = 1;
    }
    a0 = *(unsigned char *)(clip + 0x260);
    a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !slot[9])
        reset = 1;
    slot[9] = (a0 || a1) ? 1u : 0u;
    if (reset)
        ((bg_restart_fn)BG_RESTART_ADDR)(ratio);

    ((bg_mix_fn)BG_MIX_ADDR)(bg_read, &c, dl, dr ? dr : dl, n, ratio);
    ((buf_env_fn)0x080762d9u)((void *)(lane + 0x14c), scratch);
    ((buf_mix_fn)0x0804da6du)((void *)mix, scratch);
    return 1;
}
