#include "capi.h"

#include <stddef.h>

_Static_assert(offsetof(CapiVoice, owner) == SMOOTH_OFF_OWNER, "owner offset");
_Static_assert(offsetof(CapiVoice, reset_req) == SMOOTH_OFF_RESET, "reset offset");
_Static_assert(offsetof(CapiVoice, block_done) == SMOOTH_OFF_BLOCK, "block offset");

#define KMASK (CAPI_KF - 1)
#define TMASK (CAPI_TR - 1)
#define CAPI_SHIFT 32768
#define CAPI_BATCH 64   /* pump overshoot: a call per ~64 samples, not per sample */

/* SVF lowpass at fc 0.0014 (~34 Hz), q 0: g = tan(pi/2 * fc), r = 1. */
#define SVF_G   0.0021991184f
#define SVF_NUM 2.0021991184f
#define SVF_DEN 1.0044030729f
/* One-pole envelope average, tau 0.1 s at 48 kHz: g/(1+g), g = tan(1/9600). */
#define AVG_GAIN 0.00010415582f

static float fabs_(float x)
{
    return x < 0.f ? -x : x;
}

static float smoothstep(float p)
{
    return p * p * (3.f - 2.f * p);
}

void capi_init(CapiVoice *v)
{
    /* Byte stores: a uint32_t loop is a strict-aliasing violation over the
     * float fields, and -O2 then hoists the caller's saved tunables past it. */
    unsigned char *w = (unsigned char *)v;
    unsigned i;
    for (i = 0; i < sizeof(CapiVoice); i++)
        w[i] = 0;
    v->magic = CAPI_MAGIC;
    v->xgain = 1.f;
    v->leash = CAPI_LEASH;
    v->fade = CAPI_FADE;
    v->eps = CAPI_EPS;
    v->thresh = CAPI_THRESH;
    v->onset = 1;
}

static int wrap_pos(int pos, int length)
{
    if (length <= 0)
        return 0;
    pos %= length;
    if (pos < 0)
        pos += length;
    return pos;
}

static void read_n(CapiReadFn read, void *sample, int length,
                   float *dest, int pos, int count, int chan)
{
    int i = 0;
    while (i < count) {
        int p = wrap_pos(pos + i, length);
        int n = length - p;
        if (n > count - i)
            n = count - i;
        if (n < 1)
            n = 1;
        if (read)
            read(sample, dest + i, n, p, chan);
        else {
            int k;
            for (k = 0; k < n; k++)
                dest[i + k] = 0.f;
        }
        i += n;
    }
}

/* ── Keyframe ring (SparseLine) ─────────────────────────────────────────── */

static int32_t kf_oldest(const CapiChan *c)
{
    return c->wpos > CAPI_KF ? c->wpos - CAPI_KF : 0;
}

static int32_t kf_clamp(const CapiChan *c, int32_t i)
{
    int32_t newest = c->wpos - 1, oldest = kf_oldest(c);
    if (newest < 0)
        return 0;
    if (i < oldest)
        i = oldest;
    else if (i > newest)
        i = newest;
    return i;
}

static float kt_at(const CapiChan *c, int32_t i)
{
    return c->kt[kf_clamp(c, i) & KMASK];
}

static float kv_at(const CapiChan *c, int32_t i)
{
    return (float)c->kv[kf_clamp(c, i) & KMASK] * (1.f / 32767.f);
}

static void kf_push(CapiChan *c, float t, float value)
{
    int32_t q;
    if (value > 1.f)
        value = 1.f;
    else if (value < -1.f)
        value = -1.f;
    q = (int32_t)(value * 32767.f + (value < 0.f ? -0.5f : 0.5f));
    c->kt[c->wpos & KMASK] = t;
    c->kv[c->wpos & KMASK] = (int16_t)q;
    c->last_t = t;
    c->last_v = (float)q * (1.f / 32767.f);
    c->wpos++;
}

/* Walk k->w (the later frame of the bracketing pair) onto idx and cache the
 * pair. Window/StepWindow: forward while idx is past the later frame, back
 * while it is before the earlier one. */
static void cur_seek_slow(const CapiChan *c, CapiCur *k, float idx)
{
    int32_t newest = c->wpos - 1, lo = kf_oldest(c) + 1, i = k->w;
    float d;
    /* Common case: one keyframe forward. */
    if (idx > k->t1 && i >= lo && i < newest) {
        float t = c->kt[(i + 1) & KMASK];
        if (idx <= t) {
            k->w = i + 1;
            k->t2 = k->t1;
            k->v2 = k->v1;
            k->t1 = t;
            k->v1 = (float)c->kv[(i + 1) & KMASK] * (1.f / 32767.f);
            d = t - k->t2;
            k->inv = d > 1e-5f ? 1.f / d : 0.f;
            return;
        }
    }
    if (i > newest)
        i = newest;
    if (i < lo)
        i = lo;
    while (idx > kt_at(c, i) && i < newest)
        i++;
    while (idx < kt_at(c, i - 1) && i > lo)
        i--;
    k->w = i;
    k->t1 = kt_at(c, i);
    k->t2 = kt_at(c, i - 1);
    d = k->t1 - k->t2;
    k->inv = d > 1e-5f ? 1.f / d : 0.f;
    k->v1 = kv_at(c, i);
    k->v2 = kv_at(c, i - 1);
}

/* Most samples stay inside the cached pair (a keyframe every few samples). */
static inline __attribute__((always_inline))
void cur_seek(const CapiChan *c, CapiCur *k, float idx)
{
    if (!(idx <= k->t1 && idx >= k->t2))
        cur_seek_slow(c, k, idx);
}

/* Hermite between the two keyframes (zero slope at each extremum). */
static inline __attribute__((always_inline))
float cur_read(const CapiCur *k)
{
    float tau = (k->t1 - k->idx) * k->inv, h0;
    if (tau < 0.f)
        tau = 0.f;
    else if (tau > 1.f)
        tau = 1.f;
    h0 = (2.f * tau - 3.f) * tau * tau + 1.f;
    return k->v2 + h0 * (k->v1 - k->v2);
}

/* Seat a cursor at idx, searching from window w. */
static void cur_set(const CapiChan *c, CapiCur *k, int32_t w, float idx)
{
    k->w = w;
    k->idx = idx;
    cur_seek_slow(c, k, idx);
}

/* ── Analyzer / Detector front ends (DeluxeLine<float, 8>) ──────────────── */

/* tap(r, p, k): k-th newest sample (k = 1 is the one just written at p - 1). */
static float tap(const float *r, int32_t p, int k)
{
    return r[(p - k) & 7];
}

/* DeluxeLine::ReadBSpline on four consecutive taps (newest first), frac f. */
static float bspline4(float x0, float x1, float x2, float x3, float f)
{
    float f2 = f * f;
    float b0 = 1.f + f * (-3.f + f * (3.f - f));
    float b1 = 4.f + f2 * (-6.f + 3.f * f);
    float b2 = 1.f + f * (3.f + f * (3.f - 3.f * f));
    float b3 = f2 * f;
    return (x0 * b0 + x1 * b1 + x2 * b2 + x3 * b3) * (1.f / 6.f);
}

/* Read at delay 3 - a, a in [0, 1]: taps 1..4 (r1 newest), or 2..5 at a = 0. */
static float bspline_lag3(float r1, float r2, float r3, float r4, float r5, float a)
{
    return a > 0.f ? bspline4(r1, r2, r3, r4, 1.f - a) : bspline4(r2, r3, r4, r5, 0.f);
}

static int crossed(float od, float nd)
{
    return (od > 0.f && nd <= 0.f) || (od < 0.f && nd >= 0.f);
}

/* Analyzer::Analyze over cnt samples starting at input time n; the front-end
 * ring write index is p. Stores a keyframe at each extremum of the smoothed
 * signal whose value moved by more than eps since the last one, plus a
 * boundary frame after a long quiet run (Analyzer::GuardKeyframe, here checked
 * per sample). At most one keyframe per sample. State stays in locals. */
static void analyze_run(CapiChan *c, const float *in, int32_t n, int32_t cnt,
                        float eps, int32_t p)
{
    float r1 = tap(c->raw, p, 1), r2 = tap(c->raw, p, 2), r3 = tap(c->raw, p, 3);
    float r4 = tap(c->raw, p, 4), r5 = tap(c->raw, p, 5);
    float od = c->most_d1, mv = c->most_v;
    float last_t = c->last_t, last_v = c->last_v;
    int32_t i;
    for (i = 0; i < cnt; i++, n++) {
        float md, prev;
        r5 = r4;
        r4 = r3;
        r3 = r2;
        r2 = r1;
        r1 = in[i];
        mv = (r1 + 4.f * r2 + r3) * (1.f / 6.f);
        md = (r3 - r1) * 0.5f;
        prev = od;
        od = md;
        if (c->wpos == 0) {
            kf_push(c, n > 0 ? (float)(n - 1) : 0.f, mv);
        } else if (crossed(prev, md) && fabs_(mv - last_v) > eps &&
                   fabs_((float)(n - 2) + fabs_(prev) / (fabs_(prev) + fabs_(md)) - last_t) > 1.f) {
            float alpha = fabs_(prev) / (fabs_(prev) + fabs_(md));
            kf_push(c, (float)(n - 2) + alpha, bspline_lag3(r1, r2, r3, r4, r5, alpha));
        } else if ((float)n - last_t > (float)CAPI_GUARD) {
            kf_push(c, (float)(n - 1), mv);
        } else {
            continue;
        }
        last_t = c->last_t;
        last_v = c->last_v;
    }
    p += cnt;
    c->raw[(p - 1) & 7] = r1;
    c->raw[(p - 2) & 7] = r2;
    c->raw[(p - 3) & 7] = r3;
    c->raw[(p - 4) & 7] = r4;
    c->raw[(p - 5) & 7] = r5;
    c->most_d1 = od;
    c->most_v = mv;
}

/* Detector::Analyze over cnt samples of the channel sum a (+ b), from input
 * time n: TKEO -> SVF lowpass -> envelope peak. A peak over thresh x the
 * envelope's running average, outside the hold-off, queues a transient: at the
 * envelope minimum before it (onset), or at the peak's own sub-sample time. */
static void detect_run(CapiVoice *v, const float *a, const float *b,
                       int32_t n, int32_t cnt)
{
    int32_t p = v->d_rawp, i, gap = v->d_gap;
    float x1 = tap(v->d_raw, p, 1), x2 = tap(v->d_raw, p, 2), x3 = tap(v->d_raw, p, 3);
    float e1 = tap(v->d_env, p, 1), e2 = tap(v->d_env, p, 2), e3 = tap(v->d_env, p, 3);
    float e4 = tap(v->d_env, p, 4), e5 = tap(v->d_env, p, 5);
    float s1 = v->svf_s1, s2 = v->svf_s2, avs = v->avg_s, avl = v->avg_lp;
    float od = v->prev_d1, trough = v->trough, thresh = v->thresh;
    int onset = v->onset;
    for (i = 0; i < cnt; i++, n++) {
        float val, d1, d2, tkeo, hp, bp, lp, g, nd, prev;
        x3 = x2;
        x2 = x1;
        x1 = b ? a[i] + b[i] : a[i];
        val = (x1 + 4.f * x2 + x3) * (1.f / 6.f);
        d1 = (x3 - x1) * 0.5f;
        d2 = x1 - 2.f * x2 + x3;
        tkeo = fabs_(d1 * d1 - val * d2);

        hp = (tkeo - SVF_NUM * s1 - s2) * (1.f / SVF_DEN);
        bp = SVF_G * hp + s1;
        lp = SVF_G * bp + s2;
        s1 = SVF_G * hp + bp;
        s2 = SVF_G * bp + lp;
        e5 = e4;
        e4 = e3;
        e3 = e2;
        e2 = e1;
        e1 = lp;

        g = AVG_GAIN * (lp - avs);
        avl = g + avs;
        avs = avl + g;

        nd = (e3 - e1) * 0.5f;
        prev = od;
        od = nd;
        if (prev > 0.f && nd <= 0.f)
            trough = (float)n - 1.f;
        if (prev < 0.f && nd >= 0.f) {
            float dd = prev - nd;
            float frac = dd < -1e-20f ? prev / dd : 0.5f;
            float peak = bspline_lag3(e1, e2, e3, e4, e5, frac);
            if (peak > 1e-5f && peak > thresh * avl && gap <= 0) {
                float t = (float)n - 2.f + frac;
                if (onset && trough < t) {
                    float lo = t - (float)CAPI_ONSET_MAX;
                    t = trough > lo ? trough : lo;
                }
                gap = CAPI_MIN_GAP;
                if (v->tr_head - v->tr_tail >= CAPI_TR)
                    v->tr_tail++;
                v->tr[v->tr_head & TMASK] = t;
                v->tr_head++;
            }
        }
        if (gap > 0)
            gap--;
    }
    p += cnt;
    v->d_raw[(p - 1) & 7] = x1;
    v->d_raw[(p - 2) & 7] = x2;
    v->d_raw[(p - 3) & 7] = x3;
    v->d_env[(p - 1) & 7] = e1;
    v->d_env[(p - 2) & 7] = e2;
    v->d_env[(p - 3) & 7] = e3;
    v->d_env[(p - 4) & 7] = e4;
    v->d_env[(p - 5) & 7] = e5;
    v->d_rawp = p;
    v->d_gap = gap;
    v->svf_s1 = s1;
    v->svf_s2 = s2;
    v->avg_s = avs;
    v->avg_lp = avl;
    v->prev_d1 = od;
    v->trough = trough;
}

/* ── Analyzer pump ──────────────────────────────────────────────────────── */

/* Oldest keyframe index any audible cursor still needs on this channel. */
static int32_t min_live(const CapiVoice *v, const CapiChan *c)
{
    int32_t m = c->g.w < c->s.w ? c->g.w : c->s.w;
    int g;
    for (g = 0; g < 2; g++) {
        const CapiLane *L = &c->lane[g];
        if (g != v->live && v->xgain >= 1.f)
            continue;
        if (L->act.w < m)
            m = L->act.w;
        if (L->splicing && L->tmp.w < m)
            m = L->tmp.w;
    }
    return m;
}

/* Input steps that cannot evict a live keyframe (each writes at most one). */
static int32_t kf_budget(CapiVoice *v, int nch)
{
    int32_t b = 0x7FFFFFFF;
    int ch;
    for (ch = 0; ch < nch; ch++) {
        CapiChan *c = &v->ch[ch];
        int32_t k;
        cur_seek(c, &c->s, v->grid - v->fade);
        k = CAPI_KF - 1 - (c->wpos - min_live(v, c));
        if (k < b)
            b = k;
    }
    return b;
}

/* Analyze input up to time `need`, without evicting a live keyframe. */
static void pump_kf(CapiVoice *v, void *sample, int length, int nch,
                    CapiReadFn read, int32_t need)
{
    int32_t budget = 0;
    while (v->raw_count < need) {
        int32_t n = v->raw_count, k, cnt;
        int ch;
        if (budget <= 0) {
            budget = kf_budget(v, nch);
            if (budget <= 0) {
                v->stall = 1;
                return;
            }
        }
        if (n - v->chunk_t >= v->chunk_n || n < v->chunk_t) {
            int pos = wrap_pos(n + v->base, length);
            for (ch = 0; ch < nch; ch++)
                read_n(read, sample, length, v->ch[ch].in, pos, CAPI_CHUNK, ch);
            v->chunk_t = n;
            v->chunk_n = CAPI_CHUNK;
        }
        k = n - v->chunk_t;
        cnt = need - n;
        if (cnt > v->chunk_n - k)
            cnt = v->chunk_n - k;
        if (cnt > budget)
            cnt = budget;
        for (ch = 0; ch < nch; ch++)
            analyze_run(&v->ch[ch], v->ch[ch].in + k, n, cnt, v->eps, v->rawp);
        v->rawp += cnt;
        v->raw_count = n + cnt;
        budget -= cnt;
    }
}

/* Run the detector up to time `need`. */
static void pump_det(CapiVoice *v, void *sample, int length, int nch,
                     CapiReadFn read, int32_t need)
{
    while (v->d_count < need) {
        int32_t n = v->d_count, k, cnt;
        int ch;
        if (n - v->d_chunk_t >= v->d_chunk_n || n < v->d_chunk_t) {
            int pos = wrap_pos(n + v->base, length);
            for (ch = 0; ch < nch; ch++)
                read_n(read, sample, length, v->d_in[ch], pos, CAPI_DCHUNK, ch);
            v->d_chunk_t = n;
            v->d_chunk_n = CAPI_DCHUNK;
        }
        k = n - v->d_chunk_t;
        cnt = need - n;
        if (cnt > v->d_chunk_n - k)
            cnt = v->d_chunk_n - k;
        detect_run(v, v->d_in[0] + k, nch == 2 ? v->d_in[1] + k : 0, n, cnt);
        v->d_count = n + cnt;
    }
}

/* ── Grain (Granule::Read at pitch 1) ───────────────────────────────────── */

static void splice_to_grid(CapiVoice *v, const CapiChan *c, CapiLane *L, float inc)
{
    L->tmp = c->g;
    L->tmp.idx = v->grid;
    L->inc = inc;
    L->phase = 0.f;
    L->splicing = 1;
    v->n_splice++;
}

static inline __attribute__((always_inline))
float lane_read(CapiVoice *v, const CapiChan *c, CapiLane *L, int may_splice)
{
    float y;
    cur_seek(c, &L->act, L->act.idx);
    if (L->splicing)
        cur_seek(c, &L->tmp, L->tmp.idx);

    /* Guard: the ring is full (dense keyframes, a head far off the grid).
     * Fade onto the grid within one fixed fade. */
    if (v->stall && may_splice && L->act.w != c->g.w) {
        float inc = 1.f / (v->fade < 1.f ? 1.f : v->fade);
        if (!L->splicing)
            splice_to_grid(v, c, L, inc);
        else if (L->inc < inc)
            L->inc = inc;
    }

    /* The head drifted a full grain off the grid: fade back onto it over the
     * time span of `leash` keyframes on the side the grid is moving into. */
    if (!L->splicing && may_splice) {
        int32_t d = L->act.w - c->g.w;
        if ((d < 0 ? -d : d) > v->leash) {
            int32_t g = c->g.w;
            float dur;
            if (d < 0)
                dur = kt_at(c, g - 1) - kt_at(c, g - v->leash);
            else
                dur = kt_at(c, g + v->leash - 1) - kt_at(c, g - 1);
            if (dur < 48.f)
                dur = 48.f;
            splice_to_grid(v, c, L, 1.f / dur);
        }
    }

    if (L->splicing) {
        if (L->phase <= 1.f) {
            float in = cur_read(&L->tmp);
            float out = cur_read(&L->act);
            float w = smoothstep(L->phase);
            L->tmp.idx += 1.f;
            L->act.idx += 1.f;
            L->phase += L->inc;
            return in * w + out * (1.f - w);
        }
        L->act = L->tmp;
        L->splicing = 0;
    }
    y = cur_read(&L->act);
    L->act.idx += 1.f;
    return y;
}

/* Trade grains: seat the idle one so it reaches the transient at time t just
 * as the grid does, and fade it in over `fade` output samples. */
static void punch(CapiVoice *v, int nch, float t, float ratio)
{
    int g = v->live ^ 1, ch;
    float seat = t - (t - v->grid) / ratio;
    for (ch = 0; ch < nch; ch++) {
        CapiChan *c = &v->ch[ch];
        CapiLane *L = &c->lane[g];
        cur_set(c, &L->act, c->g.w, seat);
        L->splicing = 0;
        L->phase = 0.f;
    }
    v->live = g;
    v->xgain = 0.f;
    v->xstep = 1.f / (v->fade < 1.f ? 1.f : v->fade);
    v->gate = v->fade;
    v->kf_left = 0;
    v->n_punch++;
}

static void cur_shift(CapiCur *k, float s)
{
    k->idx -= s;
    k->t1 -= s;
    k->t2 -= s;
}

static void rebase(CapiVoice *v, int length, int nch)
{
    const float s = (float)CAPI_SHIFT;
    int ch, i;
    v->grid -= s;
    v->raw_count -= CAPI_SHIFT;
    v->chunk_t -= CAPI_SHIFT;
    v->d_count -= CAPI_SHIFT;
    v->d_chunk_t -= CAPI_SHIFT;
    v->trough -= s;
    v->base = wrap_pos(v->base + CAPI_SHIFT, length);
    for (i = 0; i < CAPI_TR; i++)
        v->tr[i] -= s;
    for (ch = 0; ch < nch; ch++) {
        CapiChan *c = &v->ch[ch];
        for (i = 0; i < CAPI_KF; i++)
            c->kt[i] -= s;
        c->last_t -= s;
        cur_shift(&c->g, s);
        cur_shift(&c->s, s);
        for (i = 0; i < 2; i++) {
            cur_shift(&c->lane[i].act, s);
            cur_shift(&c->lane[i].tmp, s);
        }
    }
}

void capi_process(CapiVoice *v, void *sample, int length, int stereo,
                  CapiReadFn read, float *dst_l, float *dst_r,
                  int n, float ratio, int reset)
{
    int nch = stereo ? 2 : 1, i, ch;

    if (!v || !dst_l || n <= 0 || length <= 0)
        return;
    if (v->magic != CAPI_MAGIC)
        capi_init(v);
    if (reset) {
        uint32_t owner = v->owner;
        int32_t block = v->block_done, prev = v->prev_active, leash = v->leash;
        int32_t onset = v->onset;
        float fade = v->fade, eps = v->eps, thresh = v->thresh;
        capi_init(v);
        v->owner = owner;
        v->block_done = block;
        v->prev_active = prev;
        v->leash = leash;
        v->fade = fade;
        v->eps = eps;
        v->thresh = thresh;
        v->onset = onset;
    }
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    v->ratio = ratio;
    if (!v->have) {
        v->have = 1;
        v->live = 0;
        v->xgain = 1.f;
        pump_kf(v, sample, length, nch, read, CAPI_GUARD + 8);
        for (ch = 0; ch < nch; ch++) {
            CapiChan *c = &v->ch[ch];
            cur_set(c, &c->g, 0, 0.f);
            cur_set(c, &c->s, 0, -v->fade);
            cur_set(c, &c->lane[0].act, 0, 0.f);
        }
    }

    for (i = 0; i < n; i++) {
        float y[2];
        v->stall = 0;
        {
            float det = v->grid + v->fade * ratio + 8.f;
            if (v->onset)
                det += (float)CAPI_ONSET_MAX;
            if (v->d_count < (int32_t)det)
                pump_det(v, sample, length, nch, read, (int32_t)det + CAPI_BATCH);
        }
        /* Heads (1x) and grid (ratio x) only move forward, so after a pump with
         * overshoot the front needs no look for a while. A punch resets it. */
        if (--v->kf_left <= 0) {
            float front = v->grid, step = ratio > 1.f ? ratio : 1.f;
            int32_t need;
            for (ch = 0; ch < nch; ch++) {
                int g;
                for (g = 0; g < 2; g++) {
                    const CapiLane *L = &v->ch[ch].lane[g];
                    if (g != v->live && v->xgain >= 1.f)
                        continue;
                    if (L->act.idx > front)
                        front = L->act.idx;
                    if (L->splicing && L->tmp.idx > front)
                        front = L->tmp.idx;
                }
            }
            need = (int32_t)(front + (float)CAPI_GUARD) + 8;
            if (v->raw_count < need)
                pump_kf(v, sample, length, nch, read, need + CAPI_BATCH);
            v->kf_left = (int32_t)((float)(v->raw_count - need) / step);
            /* Ring full: the live cursors free keyframes slowly, retry later. */
            if (v->stall && v->kf_left < 16)
                v->kf_left = 16;
        }

        /* Due transients: punch now so the new grain lands on the hit. */
        while (v->tr_head != v->tr_tail) {
            float t = v->tr[v->tr_tail & TMASK];
            if (v->grid + v->fade * ratio < t)
                break;
            v->tr_tail++;
            if (v->gate <= 0.f && v->grid < t)
                punch(v, nch, t, ratio);
        }

        for (ch = 0; ch < nch; ch++) {
            CapiChan *c = &v->ch[ch];
            CapiLane *in = &c->lane[v->live];
            cur_seek(c, &c->g, v->grid);
            if (v->xgain >= 1.f) {
                y[ch] = lane_read(v, c, in, 1);
            } else {
                float a = lane_read(v, c, in, 0);
                float b = lane_read(v, c, &c->lane[v->live ^ 1], 1);
                float w = smoothstep(v->xgain);
                y[ch] = a * w + b * (1.f - w);
            }
        }
        if (v->xgain < 1.f) {
            v->xgain += v->xstep;
            if (v->xgain > 1.f)
                v->xgain = 1.f;
        }
        if (v->gate > 0.f)
            v->gate -= 1.f;
        v->grid += ratio;
        if (v->grid >= CAPI_REBASE)
            rebase(v, length, nch);

        dst_l[i] += y[0];
        if (dst_r)
            dst_r[i] += nch == 2 ? y[1] : y[0];
    }
}
