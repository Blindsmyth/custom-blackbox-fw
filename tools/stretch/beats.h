/* Clip Warp "Beats" / "Slice": segment player that follows the host tempo.
 *
 * Same idea as stock Stretch with Sync (3.1.9 FUN_08065550): the clip is cut
 * into segments, and on each host-time segment boundary a grain starts at that
 * segment of the source and plays at 1x. Segments are either the Sync grid
 * (Beats) or the clip's slice markers (Slice).
 *
 * What changes is the gap at slower tempos, when a segment's own audio runs out
 * before the next one is due. Stock runs on into the next segment, which then
 * plays that audio again (the stutter). Beats fills the gap instead:
 *   LOOP   loop the segment's tail, loop length picked by waveform match
 *   GATE   fade out at the segment end
 *   RUNON  stock behaviour, for A/B
 *   PINGPONG  bounce over the segment's tail: backwards from the end, then
 *          forwards again, until the next segment starts
 *   GRANULAR  dissolve into a cloud of short grains sprayed over the tail
 *          (size, density, spread), until the next segment starts
 * Faster tempos cut a segment early with a short fade, as stock does.
 *
 * Freestanding C (host, wasm and cave). PCM comes through a read callback.
 */
#ifndef BEATS_H
#define BEATS_H

#include <stdint.h>

#define BEATS_HEADS     4     /* read heads: 2 grains x (head + loop crossfade) */
#define BEATS_CACHE     64    /* samples cached per head and channel */
#define BEATS_MATCH     128   /* loop-point match window */
#define BEATS_LOOP_MAX  4800  /* longest tail loop (100 ms) */
#define BEATS_LOOP_MIN  240   /* shortest tail loop (5 ms) */
#define BEATS_SCRATCH   (BEATS_LOOP_MAX + BEATS_MATCH)

enum { BEATS_LOOP, BEATS_GATE, BEATS_RUNON, BEATS_PINGPONG, BEATS_GRANULAR };

#define BEATS_CLOUD     8     /* granular fill: grains at once */

typedef int (*BeatsReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct BeatsHead {
    int32_t pos;
    int32_t dir;          /* +1 forward, -1 reverse (ping-pong) */
    int32_t cache_pos;
    int32_t cache_n;
    float cl[BEATS_CACHE];
    float cr[BEATS_CACHE];
} BeatsHead;

typedef struct BeatsGrain {
    int32_t active;
    int32_t seg;          /* segment index */
    int32_t end;          /* source end of its segment (unwrapped) */
    int32_t age;          /* output samples since start */
    int32_t fade_out;     /* > 0: samples of fade-out left */
    int32_t loop_len;     /* 0 until the tail loop is chosen */
    int32_t xf;           /* crossfade progress onto head b, -1 when idle */
    int32_t xf_len;
    int32_t gated;
    int32_t pp_lo;        /* ping-pong: lower turn point, 0 when not bouncing */
    int32_t cloud;        /* granular fill running */
    BeatsHead a;          /* playing head */
    BeatsHead b;          /* loop head during a crossfade */
} BeatsGrain;

typedef struct BeatsCloudGrain {
    int32_t active;
    int32_t age;
    int32_t len;
    BeatsHead h;
} BeatsCloudGrain;

typedef struct BeatsVoice {
    uint32_t magic;
    int32_t host_i;       /* host position in source samples, [0, length): */
    float host_f;         /* integer + fraction, so adding the ratio never drifts */
    int32_t cur;          /* index of the current segment, -1 before launch */
    int32_t live;         /* grain slot of the current segment */
    int32_t seg_lo;       /* source span of the current segment */
    int32_t seg_hi;
    float lead_prev;      /* last pre-roll position, to spot the loop wrap */
    /* Tunables. */
    int32_t fill;         /* BEATS_LOOP / GATE / RUNON */
    int32_t attack;       /* pre-roll fade-in, ends on the segment start (samples) */
    int32_t cut;          /* fade-out when the next segment starts */
    int32_t loop_xf;      /* loop jump crossfade (at most half the loop) and gate fade */
    int32_t pp_span;      /* ping-pong width / grain spread (samples), capped at the segment */
    int32_t grain_len;    /* granular fill: grain length (samples) */
    int32_t density;      /* granular fill: grains overlapping */
    int32_t cloud_owner;  /* grain slot the cloud belongs to, -1 = none */
    int32_t cloud_age;
    int32_t spawn_in;
    uint32_t rng;
    int32_t n_loops;      /* stats */
    BeatsGrain g[2];
    BeatsCloudGrain cloud[BEATS_CLOUD];
    float scratch[BEATS_SCRATCH];
} BeatsVoice;

#define BEATS_MAGIC 0x42454154u /* 'BEAT' */

void beats_init(BeatsVoice *v);
/* starts: ascending segment start positions, starts[0] is the loop start.
 * ratio > 1 is a faster host tempo. */
void beats_process(BeatsVoice *v, void *sample, int length, int stereo,
                   BeatsReadFn read, const int32_t *starts, int n_starts,
                   float *dst_l, float *dst_r, int n, float ratio, int reset);

/* Onset detector for clips without slices (host/lab; the device has its own
 * slice table). Returns the number of starts written, always starts[0] = 0. */
int beats_detect(void *sample, int length, int stereo, BeatsReadFn read,
                 int32_t *starts, int max_starts, float sensitivity, int min_gap);

/* Slice markers with the Sync grid filling long gaps. Inside any gap between
 * consecutive markers (or the last marker and the loop end) longer than
 * max_gap samples, grid points k * step that lie at least step / 2 from both
 * ends are added. max_gap <= 0 keeps the slices as they are. Returns the
 * number of starts written to out (ascending). */
int beats_fill_gaps(const int32_t *slices, int n, int length, float step,
                    float max_gap, int32_t *out, int max_out);

#endif
