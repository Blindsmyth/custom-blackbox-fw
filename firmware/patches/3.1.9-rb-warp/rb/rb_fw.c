/* Bank-1 wrapper: same call shape as smooth_mix; RB DSP in bank 2 (XIP). */
#include <stdint.h>
#include "bank2_syms.h"

extern int b2_live(void);

#define RB_MAGIC   0x52422020u
#define SM_BASE    0x38007000u

typedef int (*rb_init_fn)(float);
typedef void (*rb_reset_fn)(void);
typedef void (*rb_mix_fn)(const float *, const float *, float *, float *, int, float);

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

static int fw_read(void *sample, float *dest, int count, int pos, int chan)
{
    return ((pcm_read_fn)0x08063635u)(sample, dest, count, pos, chan);
}

int rb_mix_fw(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    uint32_t *slot = (uint32_t *)SM_BASE;
    void *sample, *scratch;
    float *dl, *dr;
    float in_l[2048], in_r[2048];
    int length, stereo, n, i, pos;
    float ratio;
    rb_init_fn init = (rb_init_fn)RB_INIT_ADDR;
    rb_reset_fn reset = (rb_reset_fn)RB_RESET_ADDR;
    rb_mix_fn rbmix = (rb_mix_fn)RB_MIX_ADDR;

    if (!b2_live())
        return 0;
    if (clip == 0 || lane == 0 || mix == 0 || ctx == 0)
        return 0;
    sample = *(void **)(clip + 0x24);
    if (!sample)
        return 0;
    length = ((pcm_len_fn)0x080636f1u)(sample);
    stereo = ((pcm_st_fn)0x08063791u)(sample) ? 1 : 0;
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

    if (slot[0] != RB_MAGIC || slot[1] != clip) {
        if (slot[0] != 0 && slot[0] != RB_MAGIC)
            return 0;
        slot[0] = RB_MAGIC;
        slot[1] = clip;
        init(48000.f);
        reset();
        init(48000.f);
    }
    if (slot[5]) {
        reset();
        init(48000.f);
        slot[5] = 0;
    }

    ratio = *(float *)(clip + 0xBB4);
    if (ratio < 1e-3f)
        ratio = 1.f;

    pos = (int)slot[8];
    if (pos < 0 || (length > 0 && pos >= length))
        pos = 0;
    for (i = 0; i < n; i++) {
        float L = 0.f, R = 0.f;
        int p = pos + i;
        if (length > 0)
            p %= length;
        fw_read(sample, &L, 1, p, 0);
        if (stereo)
            fw_read(sample, &R, 1, p, 1);
        else
            R = L;
        in_l[i] = L;
        in_r[i] = R;
    }
    {
        float step = ratio > 0.f ? ratio : 1.f;
        pos += (int)(n * step);
        if (length > 0)
            pos %= length;
        slot[8] = (uint32_t)pos;
    }

    rbmix(in_l, in_r, dl, dr ? dr : dl, n, ratio);
    ((buf_env_fn)0x080762d9u)((void *)(lane + 0x14c), scratch);
    ((buf_mix_fn)0x0804da6du)((void *)mix, scratch);
    return 1;
}
