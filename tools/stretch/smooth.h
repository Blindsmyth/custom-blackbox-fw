/* One-voice waveform-similarity overlap-add for Clip Warp "Smooth".
 * Window 2048, hop 1024, search ±160 at 48 kHz. Host and cave share this file.
 * Correlation uses SMOOTH_MATCH samples of the hop (not the full 1024) and
 * stops a candidate as soon as its partial SAD can no longer win.
 */
#ifndef SMOOTH_H
#define SMOOTH_H

#include <stdint.h>

#define SMOOTH_WIN    2048
#define SMOOTH_HOP    1024
#define SMOOTH_SEARCH 160
#define SMOOTH_MATCH  512
#define SMOOTH_SPAN   (SMOOTH_WIN + 2 * SMOOTH_SEARCH)
#define SMOOTH_MAGIC  0x534D5448u /* 'SMTH' */
#define SMOOTH_BASE   0x38007000u

/* Asm in the warp caves uses these byte offsets. */
#define SMOOTH_OFF_OWNER     4
#define SMOOTH_OFF_RESET     20
#define SMOOTH_OFF_BLOCK     24

typedef int (*SmoothReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct SmoothVoice {
    uint32_t magic;       /* 0 */
    uint32_t owner;       /* 4  clip pointer, 0 if the slot is free */
    float in_pos;         /* 8 */
    float ratio;          /* 12 */
    int32_t primed;       /* 16 */
    int32_t reset_req;    /* 20 set by the grain-start hook on a new launch */
    int32_t block_done;   /* 24 one mix per audio block */
    int32_t emitted;      /* 28 samples of the current hop already copied */
    int32_t have_grain;   /* 32 */
    int32_t prev_active;  /* 36 lane was sounding last block */
    float out_l[SMOOTH_HOP];   /* finished hop being emitted */
    float out_r[SMOOTH_HOP];
    float tail_l[SMOOTH_HOP];  /* second half of the last grain, added into the next hop */
    float tail_r[SMOOTH_HOP];
    float prev_l[SMOOTH_HOP];
    float scratch[SMOOTH_SPAN];
} SmoothVoice;

void smooth_init(SmoothVoice *v);
void smooth_process(SmoothVoice *v, void *sample, int length, int stereo,
                    SmoothReadFn read, float *dst_l, float *dst_r,
                    int n, float ratio, int reset);

#endif
