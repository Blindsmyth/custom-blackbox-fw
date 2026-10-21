/* Clip Warp "Deluge": port of the Synthstrom Deluge time stretcher
 * (DelugeFirmware src/deluge/dsp/timestretch/time_stretcher.cpp, hopEnd(), and
 * Sample::fillPercCache / getAveragesForCrossfade; GPL-3.0).
 *
 * Hops of output length W ("beams"). At each hop end a new play head starts so
 * that the beam is centred on where the play position will be mid-hop: source
 * [pos + W(r-1)/2, pos + W(r+1)/2]. W is picked in [min, max] (tables by stretch
 * ratio) to maximise the average percussiveness the beam covers, so hops tend
 * to land on hits. The new head is then slid by up to ~±10 ms to line up three
 * short moving averages with the old head at mid-crossfade, and the two cross-
 * fade linearly over W * proportional + absolute (tables again).
 *
 * Percussiveness: per 128-sample pixel, the relative jump of a twice-smoothed
 * |sample difference|, through tanh. Built once per clip (deluge_perc).
 *
 * Changes for the Blackbox clip: 48 kHz (hop and crossfade tables scaled from
 * the Deluge's 44.1 kHz), float instead of the Deluge's fixed point, the clip
 * loops by wrapping reads (no pre-margin crossfade), and no stretching at
 * exactly 1x. Freestanding C (host, wasm, cave); PCM through a read callback.
 */
#ifndef DELUGE_H
#define DELUGE_H

#include <stdint.h>

#define DELUGE_PIX_SHIFT  7                       /* kPercBufferReductionMagnitude */
#define DELUGE_PIX        (1 << DELUGE_PIX_SHIFT) /* samples per percussiveness pixel */
#define DELUGE_CACHE      64
#define DELUGE_AVG        38                      /* kMovingAverageLength 35 @ 44.1k */
#define DELUGE_N_AVG      3                       /* kNumMovingAverages */
#define DELUGE_SEARCH_MAX 533                     /* 48000 / 45 / 2 */
#define DELUGE_SCRATCH    (2 * DELUGE_SEARCH_MAX + DELUGE_N_AVG * DELUGE_AVG + 8)

typedef int (*DelugeReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct DelugeHead {
    int32_t pos;
    int32_t active;
    int32_t cache_pos;
    int32_t cache_n;
    float cl[DELUGE_CACHE];
    float cr[DELUGE_CACHE];
} DelugeHead;

typedef struct DelugeVoice {
    uint32_t magic;
    int32_t host_i;       /* ideal play position in source samples: integer */
    float host_f;         /* + fraction (no drift, no doubles on the SP FPU) */
    int32_t started;
    int32_t til_hop;      /* output samples until the next hop */
    float xf;             /* crossfade progress 0..1 */
    float xf_inc;
    uint32_t noise;
    DelugeHead newer;
    DelugeHead older;
    int32_t n_hops;       /* stats */
    float scratch_old[DELUGE_N_AVG * DELUGE_AVG + 8];
    float scratch[DELUGE_SCRATCH];
} DelugeVoice;

#define DELUGE_MAGIC 0x444C4745u /* 'DLGE' */

void deluge_init(DelugeVoice *v);

/* Percussiveness, one byte per DELUGE_PIX samples: n_pix = length / DELUGE_PIX + 1. */
void deluge_perc(void *sample, int length, int stereo, DelugeReadFn read,
                 uint8_t *perc, int n_pix);

/* ratio > 1 is a faster host tempo. */
void deluge_process(DelugeVoice *v, void *sample, int length, int stereo,
                    DelugeReadFn read, const uint8_t *perc, int n_pix,
                    float *dst_l, float *dst_r, int n, float ratio, int reset);

#endif
