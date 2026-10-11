/* Clip mix hook for Warp Vari. Epoch map in SRAM4 after the shared voice.
 *
 * Full AMDF over a long sample will blow pcmStreamer's 8 KB stack and the
 * audio watchdog if done in one shot. Analyze is stepped a few frames per
 * mix; reset does not rebuild the map (only a sample change does).
 */
#include "vari.h"

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

/* Past SmoothVoice (~0x3800E578). Fits before SRAM4 end 0x38010000. */
#define VARI_MAP_BASE      0x3800E800u
#define VARI_MAP_MAGIC     0x564D4150u /* 'VMAP' */
#define VARI_FW_MAX_MARKS  1400
#define VARI_STEP_BUDGET   4

typedef struct VariMap {
    uint32_t magic;
    uint32_t sample;
    VariAnalyze an;
    uint32_t marks[VARI_FW_MAX_MARKS];
} VariMap;

_Static_assert(sizeof(VariMap) <= 0x1800, "VariMap must fit in SRAM4 tail");

static int fw_read(void *sample, float *dest, int count, int pos, int chan)
{
    pcm_read_fn fn = (pcm_read_fn)0x08063635u;
    return fn(sample, dest, count, pos, chan);
}

static VariMap *map_prepare(VariVoice *v, void *sample, int length, int stereo)
{
    VariMap *m = (VariMap *)VARI_MAP_BASE;

    if (m->magic != VARI_MAP_MAGIC || m->sample != (uint32_t)sample) {
        m->magic = VARI_MAP_MAGIC;
        m->sample = (uint32_t)sample;
        vari_analyze_begin(&m->an);
    }

    if (!m->an.done) {
        /* Scratch lives in the voice — not on the 8 KB pcmStreamer stack. */
        vari_analyze_step(&m->an, sample, length, stereo, fw_read,
                          m->marks, VARI_FW_MAX_MARKS,
                          v->scratch, VARI_SCRATCH_N, VARI_STEP_BUDGET);
    }
    return m;
}

void vari_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    VariVoice *v = (VariVoice *)VARI_BASE;
    VariMap *map;
    void *sample;
    void *scratch;
    float *dl;
    float *dr;
    int length, stereo, n, reset, n_marks;
    float ratio;
    unsigned char a0, a1;
    const uint32_t *marks;
    uint32_t tmp_marks[2];

    if (v->magic != VARI_MAGIC) {
        unsigned owner = v->owner;
        int block = v->block_done;
        int req = v->reset_req;
        vari_init(v);
        v->owner = owner;
        v->block_done = block;
        v->reset_req = req;
    }
    if (clip == 0 || lane == 0 || mix == 0 || ctx == 0)
        return;
    sample = *(void **)(clip + 0x24);
    if (!sample)
        return;
    length = ((pcm_len_fn)0x080636f1u)(sample);
    stereo = ((pcm_st_fn)0x08063791u)(sample) ? 1 : 0;
    scratch = ((buf_slot_fn)0x0805f3a5u)((void *)ctx, 1);
    if (!scratch)
        return;
    ((buf_clear_fn)0x0804d8ddu)(scratch);
    n = ((buf_len_fn)0x0804d8e9u)((void *)mix);
    if (n <= 0)
        return;
    if (n > 2048)
        n = 2048;
    ((buf_setlen_fn)0x0804d8d1u)(scratch, n);
    dl = 0;
    dr = 0;
    ((buf_planes_fn)0x0804d9c1u)(scratch, &dl, &dr);
    if (!dl)
        return;
    ratio = *(float *)(clip + 0xBB4);
    reset = v->reset_req;
    v->reset_req = 0;
    a0 = *(unsigned char *)(clip + 0x260);
    a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !v->prev_active)
        reset = 1;
    v->prev_active = (a0 || a1) ? 1 : 0;

    /* Never rebuild the map on retrigger — that was a multi-second AMDF on
     * the audio thread and crashed the box when selecting Vari. */
    map = map_prepare(v, sample, length, stereo);
    if (!map)
        return;
    if (map->an.n_marks >= 2) {
        marks = map->marks;
        n_marks = map->an.n_marks;
    } else {
        /* First blocks before the stepped analyze has enough marks. */
        tmp_marks[0] = 0;
        tmp_marks[1] = length > 1 ? (uint32_t)(length - 1) : 0;
        marks = tmp_marks;
        n_marks = 2;
    }
    vari_process(v, sample, length, stereo, fw_read, marks, n_marks,
                 dl, dr, n, ratio, reset);
    ((buf_env_fn)0x080762d9u)((void *)(lane + 0x14c), scratch);
    ((buf_mix_fn)0x0804da6du)((void *)mix, scratch);
}
