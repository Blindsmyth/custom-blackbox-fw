/* Clip mix hook. Writes through the stock scratch + envelope + accumulate path. */
#include "smooth.h"

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
    pcm_read_fn fn = (pcm_read_fn)0x08063635u;
    return fn(sample, dest, count, pos, chan);
}

void smooth_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    SmoothVoice *v = (SmoothVoice *)SMOOTH_BASE;
    void *sample;
    void *scratch;
    float *dl;
    float *dr;
    int length, stereo, n, reset;
    float ratio;
    unsigned char a0, a1;

    if (v->magic != SMOOTH_MAGIC) {
        unsigned owner = v->owner;
        int block = v->block_done;
        int req = v->reset_req;
        smooth_init(v);
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
    /* Write-planes: zeros an empty buffer and marks +0x11 so accumulate keeps it. */
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
    smooth_process(v, sample, length, stereo, fw_read, dl, dr, n, ratio, reset);
    ((buf_env_fn)0x080762d9u)((void *)(lane + 0x14c), scratch);
    ((buf_mix_fn)0x0804da6du)((void *)mix, scratch);
}
