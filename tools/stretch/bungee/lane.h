/* The stock Clip lane's voice, for bank-2 code that renders a lane itself instead of stock's
 * grains (bungee.cpp for Bungee, lanes.cpp for Stutter on slices / Fill / Chop / Grain).
 *
 * The per-lane render FUN_080667B8 (repitch.S hook_lane replaces it for those Clips) does more
 * than play grains: the grain reader FUN_08066454 applies the pad's Level (with velocity and its
 * mods) and pan to every grain, the lane runs the amp envelope, the pad filter, posts the pad
 * feedback messages, and ends the voice. Everything here mirrors one of those steps, so a lane we
 * render behaves like a stock one. Audio task only. C and C++. */
#ifndef LANE_H
#define LANE_H

#include <stdint.h>

/* Stock 3.1.9 functions. */
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
#define FW_PCM_READ   ((pcm_read_fn)0x08063635u)
#define FW_PCM_LEN    ((pcm_len_fn)0x080636f1u)
#define FW_PCM_STEREO ((pcm_st_fn)0x08063791u)
#define FW_BUF_SLOT   ((buf_slot_fn)0x0805f3a5u)
#define FW_BUF_CLEAR  ((buf_clear_fn)0x0804d8ddu)
#define FW_BUF_LEN    ((buf_len_fn)0x0804d8e9u)
#define FW_BUF_SETLEN ((buf_setlen_fn)0x0804d8d1u)
#define FW_BUF_PLANES ((buf_planes_fn)0x0804d9c1u)
#define FW_BUF_ENV    ((buf_env_fn)0x080762d9u)   /* (lane+0x14C, buffer): the pad filter */
#define FW_BUF_MIX    ((buf_mix_fn)0x0804da6du)
/* The lane's voice: STOP, note-off and mute only arm a release or stop the grains; the lane render
 * runs the amp envelope, sees it finish, and deactivates the lane. */
typedef void (*env_apply_fn)(void *, float *, float *, int);
typedef void (*obj_fn)(void *);
#define FW_ENV_APPLY  ((env_apply_fn)0x0806a6c1u)   /* (lane+0x258, L, R, n): gains in place, advances stages */
#define FW_ENV_REL    ((obj_fn)0x0806a63du)         /* release (stages 1-3 -> 4) */
#define FW_ENV_FAST   ((obj_fn)0x0806a699u)         /* fast release, 256 samples */
#define FW_ENV_RESET  ((obj_fn)0x0806a5d9u)
#define FW_AUX_RESET  ((obj_fn)0x0806ef39u)         /* lane+0xF0 */
#define FW_FILT_RESET ((obj_fn)0x08076705u)         /* lane+0x14C, the pad filter */
/* Pad feedback: stock's lane render posts these to the engine's outbox (ctx + 0x15238); the pads
 * view and the waveform playhead show "playing" only from them (0x4A every block, position and
 * permille; 0x1D when a quantised launch's delay is over). */
typedef void (*post_fn)(void *, const void *);
typedef void (*tick_fn)(int64_t *, void *, int);
#define FW_POST       ((post_fn)0x0804f505u)        /* (ctx + 0x15238, 24-byte message) */
#define FW_TICK_AT    ((tick_fn)0x08069c45u)        /* (&tick, transport, offset): tick at now + offset */
/* Level and pan (FUN_08066454): a lane's param objects sum their base value and mod routes
 * (velocity is mod source 0xD, so it reaches Level only through a route, as in stock). */
typedef float (*param_val_fn)(void *);
typedef float (*level_gain_fn)(float);
#define FW_PARAM_VAL  ((param_val_fn)0x0806fb31u)   /* base + mods, clamped: lane+0x0C pan, +0x58 level, +0x94 pitch */
#define FW_LEVEL_GAIN ((level_gain_fn)0x08060881u)  /* normalised level (0..1.1875) -> gain, 0 at -64 dB */
#define OUTBOX        0x15238u
#define LANE_A        0x260u                        /* clip+0x260 and clip+0x670: a Clip's two lanes */
#define LANE_B        0x670u
#define L8(l, o)   (*(volatile uint8_t *)((l) + (o)))
#define L16(l, o)  (*(volatile uint16_t *)((l) + (o)))
#define L32(l, o)  (*(volatile int32_t *)((l) + (o)))
#define L64(l, o)  (*(volatile int64_t *)((l) + (o)))
#define LF(l, o)   (*(volatile float *)((l) + (o)))

static inline int lane_on(unsigned lane) { return L8(lane, 0) && !L8(lane, 0x148); }

/* Stock's lane deactivate (FUN_080667B8 0x08066D2A-0x08066E78): the grains in the lane's list go
 * back to the Clip's four slots, the lane's fields clear, its envelope, aux and filter reset. */
static inline void lane_end(unsigned clip, unsigned lane, int free_grains)
{
    int o;
    if (free_grains) {
        int count = L32(lane, 0xD8);
        for (int g = 0; g < count && g < 4; g++) {
            int idx = L32(lane, 0xC8 + 4 * g);
            if (idx < 0 || idx > 3)
                continue;
            unsigned gr = clip + (unsigned)idx * 0x70u;
            L16(gr, 0x30) = 0;
            L32(gr, 0x68) = 0;
            L8(gr, 0x6C) = 0;
            LF(gr, 0x48) = 1.f;
            L32(gr, 0x4C) = 0;
            L32(gr, 0x50) = 0;
            L32(gr, 0x54) = 0x100;
            L32(gr, 0x58) = -1;
            L32(gr, 0x5C) = 0x100;
            L32(gr, 0x60) = 0;
            L32(gr, 0x64) = 0;
            for (o = 0x38; o < 0x48; o += 4)
                L32(gr, o) = 0;
        }
        L32(lane, 0xD8) = 0;
    }
    L8(lane, 0) = 0;
    L32(lane, 4) = 0;
    L16(lane, 8) = 0;
    for (o = 0xC8; o <= 0xD4; o += 4)
        L32(lane, o) = -1;
    L32(lane, 0x130) = 0;
    L16(lane, 0x134) = 0;
    L8(lane, 0x148) = 0;
    for (o = 0xE0; o < 0xF0; o += 4)
        L32(lane, o) = 0;
    for (o = 0x138; o < 0x148; o += 4)
        L32(lane, o) = 0;
    FW_ENV_RESET((void *)(lane + 0x258));
    FW_AUX_RESET((void *)(lane + 0xF0));
    FW_FILT_RESET((void *)(lane + 0x14C));
}

/* ---- Level and pan, as the grain reader applies them --------------------------------------
 * Stock keeps three gain ramps per grain (grain + 0x40 / 0x50 / 0x60): FUN_08075ff0 sets a
 * target and a length of |current - target| x 256 samples, at most 256; FUN_08075e40 runs it.
 * Same here, per lane, in C (stock's ramp keeps its sample count in a float slot as an int). */
typedef struct LaneRamp { float cur, target; int left; } LaneRamp;
typedef struct LaneGain { LaneRamp g, l, r; } LaneGain;

static inline void ramp_set(LaneRamp *p, float t, int snap)
{
    float d = p->cur - t;
    int n;
    if (d < 0.f)
        d = -d;
    n = (int)(d * 256.f);
    p->target = t;
    p->left = n > 256 ? 256 : n;
    if (snap || p->left == 0) {
        p->cur = t;
        p->left = 0;
    }
}

static inline void ramp_run(LaneRamp *p, float *a, float *b, int n)
{
    int i = 0;
    if (p->left > 0) {
        float step = (p->target - p->cur) / (float)p->left;
        for (; i < n && p->left > 0; i++, p->left--) {
            p->cur += step;
            a[i] *= p->cur;
            if (b)
                b[i] *= p->cur;
        }
        if (p->left == 0)
            p->cur = p->target;
    }
    if (p->cur == 1.f)
        return;
    for (; i < n; i++) {
        a[i] *= p->cur;
        if (b)
            b[i] *= p->cur;
    }
}

/* The pad's Level (velocity and mods included) and pan on the lane's n frames, l/r (r = 0: the
 * buffer is mono, pan does not apply). snap: a new note, the ramps start at their value. */
static inline void lane_gain(unsigned clip, unsigned lane, LaneGain *s, float *l, float *r, int n, int snap)
{
    float g = FW_LEVEL_GAIN(FW_PARAM_VAL((void *)(lane + 0x58)));
    float pan = FW_PARAM_VAL((void *)(lane + 0x0C));
    if (L8(clip, 0xB78))
        g = 0.f;                                  /* mute (param 100) */
    ramp_set(&s->g, g, snap);
    ramp_run(&s->g, l, r, n);
    if (r && (pan != 0.f || (uint32_t)L32(lane, 0x30) > 1u) && !L8(clip, 0xBA0)) {
        /* A balance: the far side turns down (output routing 0x95 set: no pan, as stock). */
        ramp_set(&s->l, pan <= 0.f ? 1.f : 1.f - pan, snap);
        ramp_set(&s->r, pan <= 0.f ? 1.f + pan : 1.f, snap);
        ramp_run(&s->l, l, 0, n);
        ramp_run(&s->r, r, 0, n);
    } else {
        ramp_set(&s->l, 1.f, 1);
        ramp_set(&s->r, 1.f, 1);
    }
}

/* ---- the clock -------------------------------------------------------------------------- */

/* The clock tick at output sample s (fractional): the transport's tempo map, 960 ticks per beat,
 * one tick = 3000 / BPM samples at 48 kHz (map anchor tick +0x08, sample +0x10, BPM +0x18). */
static inline double tick_at(void *transport, double s)
{
    const char *t = (const char *)transport;
    double map_tick = (double)*(const volatile int64_t *)(t + 0x08);
    double map_sample = (double)*(const volatile int64_t *)(t + 0x10);
    double bpm = *(const volatile float *)(t + 0x18);
    return map_tick + (s - map_sample) * bpm / 3000.;
}

/* e mod len, into [-len/2, len/2): how far ahead the clock is, the short way round the loop. */
static inline double wrap_err(double e, int len)
{
    e = __builtin_fmod(e, (double)len);
    if (e >= len / 2.)
        e -= len;
    if (e < -len / 2.)
        e += len;
    return e;
}

/* The clock tick at the launch's first frame: the transport's sample count at launch (lane+0xE0)
 * plus the quantise delay (lane+0x138, before the block counts it down). */
static inline double lane_launch_tick(unsigned lane, void *transport)
{
    return tick_at(transport, (double)(L64(lane, 0xE0) + L32(lane, 0x138)));
}

/* Where stock would be in the file now (source frames, unwrapped from the launch), from the clock:
 * 1 while the transport plays and the Clip knows its length in ticks, else 0. */
static inline int lane_expect(unsigned clip, unsigned lane, void *transport, double anchor, int length,
                              double *out)
{
    int64_t clip_ticks = L64(clip, 0xBA8);
    double now;
    if (!transport || !*(volatile uint8_t *)((char *)transport + 0x38) || clip_ticks <= 0 || length <= 0)
        return 0;
    now = (double)*(volatile int64_t *)transport + (L32(lane, 0x138) > 0 ? L32(lane, 0x138) : 0);
    *out = (tick_at(transport, now) - anchor) * (double)length / (double)clip_ticks;
    return 1;
}

/* Loop Mode on and not Trigger: the Clip loops; otherwise stock stops at the end of the file. */
static inline int clip_loops(unsigned clip) { return L8(clip, 0x1F4) && L8(clip, 0x1F8) != 1; }

/* The clock ticks since the launch (anchor), now, while the transport plays: 1, else 0. */
static inline int lane_ticks(unsigned lane, void *transport, double anchor, double *out)
{
    double now;
    if (!transport || !*(volatile uint8_t *)((char *)transport + 0x38))
        return 0;
    now = (double)*(volatile int64_t *)transport + (L32(lane, 0x138) > 0 ? L32(lane, 0x138) : 0);
    *out = tick_at(transport, now) - anchor;
    return 1;
}

#ifdef WARP_DECK_SEQ
/* ---- Clip Deck (the combined 3.1.T build) ------------------------------------------------------
 * Stock plays a Clip as one grain per Sync unit, and Clip Deck (3.1.9-clip-deck/deck.c) steers which
 * unit comes next: the note start's unit (Start, a cue launch, the editor's marker), Loop End -> Loop
 * Start, End, and a pending hot-cue jump on the next Quant boundary. It does that from stock's lane
 * render, which a lane rendered in bank 2 skips. DeckSeq runs the same sequence for those lanes, by
 * calling deck.c's own deck_limit / deck_wrap_unit at each unit, so Fill, Chop, Grain and Bungee follow
 * the regions and cues as Stutter and Repitch do. Bank 2 calls into the cave directly (one link). */
#ifdef __cplusplus
extern "C" {
#endif
unsigned deck_limit(void *clip, unsigned next);
unsigned deck_wrap_unit(void *clip, unsigned unit, const long long *tick);
int deck_grain_start(void *clip, int frame);
#ifdef __cplusplus
}
#endif

#define SEQ_RING 4

typedef struct DeckSeq {
    int on;                   /* the Clip is on a note-value Sync grid: deck.c steers it */
    unsigned unit[SEQ_RING];  /* unit of sequence index i at [i % SEQ_RING] */
    int decided;              /* the last index whose unit is known */
    int stop_at;              /* the index where the note ends (End, Loop off), or -1 */
    unsigned n_units;         /* units in the file (clip+0x240 + 0x244) */
    double ut;                /* ticks per unit (clip+0x208) */
    float spu;                /* file frames per unit (clip+0x1FC) */
    double units;             /* units since the launch, kept here while there is no clock */
} DeckSeq;

/* Units on a note-value Sync grid, as deck.c grid_units, or 0 (Slicer, Sync None, Sync Slice). */
static inline unsigned deck_units(unsigned clip, unsigned *ut)
{
    unsigned n;
    if (L8(clip, 0x254) || !L8(clip, 0x1F5))
        return 0;
    if ((L32(clip, 0x220) == 0xF0000 && L32(clip, 0x224) == 0) || L32(clip, 0x20C) != 0)
        return 0;
    *ut = (unsigned)L32(clip, 0x208);
    n = (unsigned)L32(clip, 0x240) + (unsigned)L32(clip, 0x244);
    if (*ut == 0 || *ut > 0xFFFF || n < 2)
        return 0;
    return n;
}

/* A new note: its first unit is the one stock's note start gave the lane's first grain (deck.c
 * deck_note_unit already chose it: Start, a cue, the editor's marker). */
static inline void seq_start(DeckSeq *q, unsigned clip, unsigned lane)
{
    unsigned ut = 0;
    int g = L32(lane, 0xC8);
    q->n_units = deck_units(clip, &ut);
    q->on = q->n_units != 0;
    q->unit[0] = g >= 0 && g <= 3 ? (unsigned)L32(clip + (unsigned)g * 0x70u, 0x64) : 0u;
    q->decided = 0;
    q->stop_at = -1;
    q->ut = ut;
    q->spu = LF(clip, 0x1FC);
    q->units = 0.;
}

/* The next index's unit, as stock's lane render chooses it at that grain (0x08066C46 limit, the
 * re-sync to unit 0 that deck_wrap_unit turns into Loop Start, the jump at 0x08066CF2). */
static inline void seq_decide(DeckSeq *q, unsigned clip, double anchor)
{
    int i = q->decided + 1;
    unsigned next = q->unit[(i - 1) % SEQ_RING] + 1u;
    if (next >= deck_limit((void *)clip, next)) {
        if (!clip_loops(clip)) {
            q->stop_at = i;
            q->unit[i % SEQ_RING] = q->unit[(i - 1) % SEQ_RING];
            q->decided = i;
            return;
        }
        next = 0;
    }
    long long tick = (long long)(anchor + (double)i * q->ut + 0.5);
    q->unit[i % SEQ_RING] = deck_wrap_unit((void *)clip, next, &tick);
    q->decided = i;
}

/* Decide every unit that starts before u + lead (units), at most two ahead (the ring keeps one behind). */
static inline void seq_advance(DeckSeq *q, unsigned clip, double anchor, double u, double lead)
{
    int now = u > 0. ? (int)u : 0, want = u + lead > 0. ? (int)(u + lead) : 0;
    if (want > now + 2)
        want = now + 2;
    while (q->stop_at < 0 && q->decided < want)
        seq_decide(q, clip, anchor);
}

static inline int seq_ended(const DeckSeq *q, double u) { return q->stop_at >= 0 && u >= (double)q->stop_at; }

/* The first file frame of index i's unit (with the pad's grid offset, as Clip_StartGrain). */
static inline int seq_unit_frame(const DeckSeq *q, unsigned clip, int i)
{
    return deck_grain_start((void *)clip, (int)((float)q->unit[i % SEQ_RING] * q->spu));
}

/* The file frame at u units since the launch. */
static inline double seq_frame(const DeckSeq *q, unsigned clip, double u)
{
    int i = u > 0. ? (int)u : 0;
    if (i > q->decided)
        i = q->decided;
    double f = u - (double)i;
    if (f < 0.)
        f = 0.;
    return (double)seq_unit_frame(q, clip, i) + f * q->spu;
}
#endif

/* ---- pad feedback ------------------------------------------------------------------------- */

/* "Started", once a quantised launch's delay is over (from: the frame of this block it starts at). */
static inline void lane_post_started(unsigned clip, unsigned lane, unsigned ctx, int from)
{
    if (L32(lane, 0x138) || L32(lane, 0xD8) <= 0)
        return;
    int g = L32(lane, 0xC8);
    unsigned gr = clip + (unsigned)g * 0x70u;
    if (g < 0 || g > 3 || !L8(gr, 0x6C))
        return;
    void *transport = *(void **)ctx;
    uint32_t m[6] = { 0x1Du, *(volatile uint32_t *)(clip + 0x248), *(volatile uint32_t *)(clip + 0x18),
                      *(volatile uint32_t *)(*(volatile uint32_t *)(clip + 0x24) + 0x24), 0, 0 };
    FW_POST((void *)(ctx + OUTBOX), m);
    if (transport)
        FW_TICK_AT((int64_t *)(lane + 0x140), transport, from);
    L8(gr, 0x6C) = 0;
}

/* The playhead: source frame `at` (in [0, length)). */
static inline void lane_post_pos(unsigned clip, unsigned ctx, double at, int length)
{
    uint32_t pos = (uint32_t)at;
    if (pos == *(volatile uint32_t *)(clip + 0x248))
        return;
    uint32_t m[6] = { 0x4Au, 0, *(volatile uint32_t *)(clip + 0x18),
                      *(volatile uint32_t *)(*(volatile uint32_t *)(clip + 0x24) + 0x24), pos, 0 };
    *(int16_t *)&m[5] = (int16_t)(at * 1000. / length);
    FW_POST((void *)(ctx + OUTBOX), m);
    *(volatile uint32_t *)(clip + 0x248) = pos;
}

/* End of voice, as stock, after the block: the envelope finished (stop, note-off, mute), a
 * quantised release came due, or the grains were stopped. 1 = the lane is off now. */
static inline int lane_finish(unsigned clip, unsigned lane, int n)
{
    if (L8(lane, 0x25C) == 5) {
        lane_end(clip, lane, 1);
        return 1;
    }
    if (L8(lane, 0x134)) {
        if (L32(lane, 0x130) >= n) {
            L32(lane, 0x130) -= n;
        } else {
            if (L8(lane, 0x135))
                FW_ENV_FAST((void *)(lane + 0x258));
            else
                FW_ENV_REL((void *)(lane + 0x258));
            FW_ENV_REL((void *)(lane + 0x258));
            L8(lane, 0x134) = 0;
        }
    }
    if (L8(lane, 9) && L32(lane, 0xD8) == 0) {
        lane_end(clip, lane, 0);
        return 1;
    }
    return 0;
}

#endif
