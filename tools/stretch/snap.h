/* Deluge-style Clip Warp "Snap": hops guided by the slice map + short crossfade.
 * Shares SM_BASE with Smooth (one owner). Host and cave share this file.
 *
 * Grains are placed at in_pos (like Smooth), not streamed then jumped — a
 * stream+jump of hop*(ratio-1) reads as pitch tracking tempo on bass when the
 * join is not period-aligned. Seek ±SNAP_SEARCH keeps joins on period.
 */
#ifndef SNAP_H
#define SNAP_H

#include <stdint.h>

#include "smooth.h" /* SMOOTH_BASE + owner/reset/block offsets */

#define SNAP_HOP     1024
#define SNAP_XF      256
#define SNAP_NEAR    64
#define SNAP_SEARCH  256 /* wide enough for ~90 Hz fundamentals */
#define SNAP_MATCH   128
#define SNAP_MAGIC   0x534E4150u /* 'SNAP' */
#define SNAP_BASE    SMOOTH_BASE

typedef int (*SnapReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct SnapVoice {
    uint32_t magic;       /* 0 */
    uint32_t owner;       /* 4 */
    float in_pos;         /* 8 */
    float ratio;          /* 12 */
    int32_t primed;       /* 16 samples already emitted from the current grain */
    int32_t reset_req;    /* 20 */
    int32_t block_done;   /* 24 */
    int32_t have;         /* 28 */
    int32_t prev_active;  /* 32 */
    int32_t slice_i;      /* 36 */
    float grain_l[SNAP_HOP];
    float grain_r[SNAP_HOP];
    float prev_l[SNAP_HOP];
    float prev_r[SNAP_HOP];
    float scratch[SNAP_MATCH + 2 * SNAP_SEARCH];
} SnapVoice;

_Static_assert(sizeof(SnapVoice) <= 29992, "SnapVoice must fit in Smooth's SRAM4 slot");

void snap_init(SnapVoice *v);
void snap_process(SnapVoice *v, void *sample, int length, int stereo,
                  SnapReadFn read, const uint32_t *slices, int n_slices,
                  float *dst_l, float *dst_r, int n, float ratio, int reset);

#endif
