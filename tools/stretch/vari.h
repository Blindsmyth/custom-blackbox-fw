/* VariPhrase-inspired Clip Warp: epoch map + map-driven grains.
 * ABI matches Smooth/Snap (SMOOTH_BASE owner/reset/block).
 *
 * Analyze builds ascending splice/epoch marks (pitch periods + onsets).
 * Process places period-centered grains on that map and XF-joins them,
 * repeating or skipping marks to stretch tempo (ratio > 1 = faster).
 *
 * Scratch for analyze is caller-owned — never put the AMDF window on the
 * pcmStreamer stack (8 KB). Firmware steps analyze across mix blocks.
 */
#ifndef VARI_H
#define VARI_H

#include <stdint.h>

#include "smooth.h" /* SMOOTH_BASE + owner/reset/block offsets */

#define VARI_HOP           1024
#define VARI_XF            256
#define VARI_MAX_GRAIN     2048
#define VARI_MIN_PERIOD    120  /* ~400 Hz @ 48 kHz */
#define VARI_MAX_PERIOD    600  /* ~80 Hz @ 48 kHz */
#define VARI_FALLBACK_PER  480  /* ~10 ms */
#define VARI_FRAME         256
#define VARI_FRAME_HOP     128
#define VARI_SCRATCH_N     (VARI_MAX_PERIOD + 128)
#define VARI_MAGIC         0x56415249u /* 'VARI' */
#define VARI_BASE          SMOOTH_BASE
#define VARI_MAX_MARKS     4096

typedef int (*VariReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct VariVoice {
    uint32_t magic;       /* 0 */
    uint32_t owner;       /* 4 */
    float in_pos;         /* 8 */
    float ratio;          /* 12 */
    int32_t primed;       /* 16 samples already emitted from the current grain */
    int32_t reset_req;    /* 20 */
    int32_t block_done;   /* 24 */
    int32_t have;         /* 28 */
    int32_t prev_active;  /* 32 */
    int32_t grain_len;    /* 36 */
    int32_t mark_i;       /* 40 */
    float grain_l[VARI_MAX_GRAIN];
    float grain_r[VARI_MAX_GRAIN];
    float prev_l[VARI_HOP];
    float prev_r[VARI_HOP];
    float scratch[VARI_SCRATCH_N];
} VariVoice;

_Static_assert(sizeof(VariVoice) <= 29992, "VariVoice must fit in Smooth's SRAM4 slot");
_Static_assert(VARI_SCRATCH_N >= VARI_MAX_PERIOD + 128, "analyze scratch");

/* Incremental analyze state (lives in the firmware map, not on the stack). */
typedef struct VariAnalyze {
    int32_t pos;
    int32_t next_epoch;
    int32_t period;
    int32_t n_marks;
    int32_t done;
    float prev_e;
} VariAnalyze;

void vari_analyze_begin(VariAnalyze *a);
/* Advance up to `budget` frame hops. Returns 1 when the map is complete. */
int vari_analyze_step(VariAnalyze *a, void *sample, int length, int stereo,
                      VariReadFn read, uint32_t *marks, int max_marks,
                      float *scratch, int scratch_n, int budget);

/* Host helper: run analyze to completion. scratch must hold VARI_SCRATCH_N floats. */
int vari_analyze(void *sample, int length, int stereo, VariReadFn read,
                 uint32_t *marks, int max_marks, float *scratch, int scratch_n);

void vari_init(VariVoice *v);
void vari_process(VariVoice *v, void *sample, int length, int stereo,
                  VariReadFn read, const uint32_t *marks, int n_marks,
                  float *dst_l, float *dst_r, int n, float ratio, int reset);

#endif
