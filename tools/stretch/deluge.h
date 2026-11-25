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
 *
 * Device options (3.1.T's per-lane Warp "Deluge", tools/stretch/bungee/lanes.cpp); without them the
 * Warp Lab's build is unchanged:
 *   DELUGE_PERC_RING   pixels (a power of two): instead of the whole file's percussiveness up front
 *                      (deluge_perc: a byte per pixel, ~110 KB for 5 minutes, and the whole file read),
 *                      each voice keeps a window of it around the play position, computed as the play
 *                      position moves on. Same smoothing as deluge_perc, its state in the voice; a jump
 *                      starts the window again after DELUGE_PERC_WARM samples of smoothing (at the
 *                      file's start exactly as deluge_perc). deluge_process's perc / n_pix go unused.
 *   DELUGE_SHARED_SCRATCH  the phase search's scratch is a pointer the caller sets (used inside one
 *                      call only, so voices can share it): DELUGE_SCRATCH_ALL floats.
 *   DELUGE_CACHE       samples cached per head and channel.
 *   DELUGE_FAST_SEARCH the phase search's moving averages from running sums (~10x cheaper; rounds a
 *                      little differently from the Lab's).
 *   DELUGE_RESYNC      frames: a play head further than this from the play position (a clock that
 *                      moved it) hops at once; at exactly 1x, where it never hops, DELUGE_RESYNC_1X.
 */
#ifndef DELUGE_H
#define DELUGE_H

#include <stdint.h>

#define DELUGE_PIX_SHIFT  7                       /* kPercBufferReductionMagnitude */
#define DELUGE_PIX        (1 << DELUGE_PIX_SHIFT) /* samples per percussiveness pixel */
#ifndef DELUGE_CACHE
#define DELUGE_CACHE      64
#endif
#define DELUGE_AVG        38                      /* kMovingAverageLength 35 @ 44.1k */
#define DELUGE_N_AVG      3                       /* kNumMovingAverages */
#define DELUGE_SEARCH_MAX 533                     /* 48000 / 45 / 2 */
#define DELUGE_SCRATCH    (2 * DELUGE_SEARCH_MAX + DELUGE_N_AVG * DELUGE_AVG + 8)
#define DELUGE_SCRATCH_OLD (DELUGE_N_AVG * DELUGE_AVG + 8)
#define DELUGE_SCRATCH_ALL (DELUGE_SCRATCH + DELUGE_SCRATCH_OLD)

#ifdef DELUGE_PERC_RING
#define DELUGE_PERC_AHEAD  112   /* pixels kept ahead of the play position (the hop search reaches ~95) */
#define DELUGE_PERC_BEHIND 16    /* and behind, from a new start (it reaches ~17 back, at 0.25x) */
#define DELUGE_PERC_JUMP   64    /* the play position this far past the window: start it again */
#define DELUGE_PERC_WARM   4096  /* samples smoothed before a new start's first pixel */
typedef struct DelugePerc {
    int32_t next;         /* the next pixel to store, counted on: pixel u is file pixel u % n_pix; 0 = none */
    int32_t lo;           /* the oldest pixel held */
    int32_t at;           /* the play position's pixel at the last step, counted on likewise */
    int32_t src;          /* the next sample into the smoothing */
    int32_t warm;         /* samples to smooth before storing */
    int32_t length;
    float prev, pole0, pole1, last;
    int32_t cur;          /* the pixel being smoothed */
    uint8_t ring[DELUGE_PERC_RING];
} DelugePerc;
#endif

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
#ifdef DELUGE_SHARED_SCRATCH
    float *scratch;       /* DELUGE_SCRATCH_ALL floats, kept across deluge_init */
#else
    float scratch_old[DELUGE_SCRATCH_OLD];
    float scratch[DELUGE_SCRATCH];
#endif
#ifdef DELUGE_PERC_RING
    DelugePerc perc;
#endif
} DelugeVoice;

#define DELUGE_MAGIC 0x444C4745u /* 'DLGE' */

void deluge_init(DelugeVoice *v);

/* Percussiveness, one byte per DELUGE_PIX samples: n_pix = length / DELUGE_PIX + 1. */
void deluge_perc(void *sample, int length, int stereo, DelugeReadFn read,
                 uint8_t *perc, int n_pix);

#ifdef DELUGE_PERC_RING
/* The window's byte for pixel p (any p, wrapped to the file; the lap nearest the play position),
 * or -1 where it does not hold it (yet). */
int deluge_perc_at(const DelugeVoice *v, int p, int n_pix);
#endif

/* ratio > 1 is a faster host tempo. */
void deluge_process(DelugeVoice *v, void *sample, int length, int stereo,
                    DelugeReadFn read, const uint8_t *perc, int n_pix,
                    float *dst_l, float *dst_r, int n, float ratio, int reset);

#endif
