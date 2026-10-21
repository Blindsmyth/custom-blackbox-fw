/* Clip Warp "Capi": Capicola keyframe time stretch, one Clip voice.
 *
 * Port of heavylight-industries/capicola (lib/KeyframeRecorder.h, Analyzer.h,
 * Granule.h, Detector.h; AGPL-3.0), after the DAFx26 paper "Keyframe Time
 * Stretching via Extrema Sampling". Host and cave share this file.
 *
 * - Analyzer: each channel is reduced to keyframes at the extrema of a 3-tap
 *   B-spline of the input (sub-sample time, value gate CAPI_EPS). Output is
 *   rebuilt between keyframes with a cubic Hermite (zero slope at extrema).
 * - Grain: the read head plays at 1x while the grid moves at the clip ratio.
 *   Once the head is more than CAPI_LEASH keyframes off the grid it crossfades
 *   back onto it, over the time span of CAPI_LEASH keyframes (adaptive grain).
 * - Punch: a TKEO -> SVF -> peak detector on L+R finds transients. A kept one
 *   seats a second grain ahead of the hit and trades the two over CAPI_FADE
 *   output samples, so the hit lands on the grid at full gain.
 *
 * Changes for a file-backed clip (Capicola stretches a live input):
 * - The analyzer runs on demand just ahead of the heads instead of at the
 *   input rate, so there is no live edge, no delayed tap and no front guard.
 *   The detector reads further ahead on its own cursor, so its lookahead costs
 *   no keyframes. The ring keeps history back to grid - CAPI_FADE (punch
 *   seats). When it is full the writer stalls, and every head off the grid
 *   fades onto it over CAPI_FADE: Capicola's front and back guards in one.
 * - One grid and one detector drive both channels (no stereo drift guardrail).
 * - The grid is the clip clock and never jumps; a punch only moves the head.
 * - Times are float relative to a moving origin (SP FPU, no doubles).
 * - Keyframe values are int16 to fit two 2048-frame rings in Smooth's slot.
 * - A punch lands at the envelope trough before the peak (the onset), not at
 *   the peak, which the 34 Hz envelope puts ~10 ms late (`onset` = 0 restores
 *   Capicola's peak landing; there the attack plays at about half gain).
 *
 * Shares SM_BASE with Smooth/Vari (one owner).
 */
#ifndef CAPI_H
#define CAPI_H

#include <stdint.h>

#include "smooth.h" /* SMOOTH_BASE + owner/reset/block offsets */

#define CAPI_KF        2048   /* keyframes per channel ring, power of 2 */
#define CAPI_CHUNK     256    /* input samples fetched per PCM read */
#define CAPI_DCHUNK    128    /* same, detector cursor */
#define CAPI_TR        16     /* pending transients, power of 2 */
#define CAPI_LEASH     642    /* grain size in keyframes (Capicola perf default) */
#define CAPI_FADE      960.f  /* punch crossfade, samples (20 ms) */
#define CAPI_EPS       0.001f /* analyzer value gate (Quality max) */
#define CAPI_THRESH    2.222f /* transient keep ratio over the envelope average */
#define CAPI_GUARD     480    /* boundary keyframe after this many quiet samples */
#define CAPI_MIN_GAP   7200   /* hold-off between kept transients (150 ms) */
#define CAPI_ONSET_MAX 1024   /* furthest an onset is placed before its peak */
#define CAPI_REBASE    65536.f
#define CAPI_MAGIC     0x43415049u /* 'CAPI' */
#define CAPI_BASE      SMOOTH_BASE

typedef int (*CapiReadFn)(void *sample, float *dest, int count, int pos, int chan);

/* A read cursor and the keyframe pair bracketing it, cached so the per-sample
 * read is one multiply-add chain with no ring access or divide. */
typedef struct CapiCur {
    float idx;
    int32_t w;            /* monotonic index of the later keyframe (Window::index) */
    float t1, t2;         /* its time and the earlier one's */
    float inv;            /* 1 / (t1 - t2), 0 for a degenerate pair */
    float v1, v2;
} CapiCur;

/* One channel of one grain: the head, and the splice cursor while fading. */
typedef struct CapiLane {
    CapiCur act;
    CapiCur tmp;
    float phase;
    float inc;
    int32_t splicing;
} CapiLane;

typedef struct CapiChan {
    float kt[CAPI_KF];    /* keyframe times */
    int16_t kv[CAPI_KF];  /* keyframe values, x32767 */
    float in[CAPI_CHUNK];
    float raw[8];         /* B-spline front end ring */
    float most_v;
    float most_d1;
    float last_t;         /* newest keyframe */
    float last_v;
    int32_t wpos;         /* keyframes ever written */
    CapiCur g;            /* bracket of the grid (idx unused) */
    CapiCur s;            /* ... and of grid - fade, the oldest punch seat */
    CapiLane lane[2];     /* one per grain */
} CapiChan;

typedef struct CapiVoice {
    uint32_t magic;       /* 0 */
    uint32_t owner;       /* 4 */
    float grid;           /* 8  clip clock, input samples from the origin */
    float ratio;          /* 12 */
    int32_t base;         /* 16 sample position of time 0, in [0, length) */
    int32_t reset_req;    /* 20 */
    int32_t block_done;   /* 24 */
    int32_t have;         /* 28 */
    int32_t prev_active;  /* 32 */
    int32_t raw_count;    /* next input time the analyzer consumes */
    int32_t chunk_t;      /* input time of in[0] */
    int32_t chunk_n;
    int32_t rawp;         /* analyzer front-end ring write index */
    int32_t d_count;      /* next input time the detector consumes */
    int32_t d_chunk_t;
    int32_t d_chunk_n;
    int32_t d_rawp;
    int32_t live;         /* grain fading in / steady */
    float xgain;
    float xstep;
    float gate;           /* punch refractory, output samples */
    int32_t stall;        /* keyframe ring full this sample */
    int32_t kf_left;      /* samples before the heads can outrun the analyzer */
    /* Tunables (proto overrides them after init). */
    int32_t leash;
    float fade;
    float eps;
    float thresh;
    int32_t onset;        /* land punches on the onset (1) or the peak (0) */
    /* Detector. */
    float d_raw[8];
    float d_env[8];
    float svf_s1, svf_s2;
    float avg_s, avg_lp;
    float prev_d1;
    float trough;         /* time of the last envelope minimum */
    int32_t d_gap;
    int32_t tr_head, tr_tail;
    float tr[CAPI_TR];
    float d_in[2][CAPI_DCHUNK];
    int32_t n_punch;      /* stats for the host */
    int32_t n_splice;
    CapiChan ch[2];
} CapiVoice;

_Static_assert(sizeof(CapiVoice) <= 29992, "CapiVoice must fit in Smooth's SRAM4 slot");

void capi_init(CapiVoice *v);
void capi_process(CapiVoice *v, void *sample, int length, int stereo,
                  CapiReadFn read, float *dst_l, float *dst_r,
                  int n, float ratio, int reset);

#endif
