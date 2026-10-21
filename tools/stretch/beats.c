#include "beats.h"

static float fabs_(float x)
{
    return x < 0.f ? -x : x;
}

static int wrap(int p, int length)
{
    p %= length;
    return p < 0 ? p + length : p;
}

/* count samples from pos (wrapping at length) into dest. */
static void read_wrap(BeatsReadFn read, void *sample, int length, float *dest,
                      int pos, int count, int chan)
{
    int i = 0;
    while (i < count) {
        int p = wrap(pos + i, length);
        int n = length - p;
        if (n > count - i)
            n = count - i;
        read(sample, dest + i, n, p, chan);
        i += n;
    }
}

void beats_init(BeatsVoice *v)
{
    unsigned char *w = (unsigned char *)v;
    unsigned i;
    for (i = 0; i < sizeof(BeatsVoice); i++)
        w[i] = 0;
    v->magic = BEATS_MAGIC;
    v->cur = -1;
    v->fill = BEATS_LOOP;
    v->attack = 256;
    v->cut = 512;
    v->loop_xf = 1024;
    v->pp_span = 2400;
    v->grain_len = 2400;
    v->density = 4;
    v->cloud_owner = -1;
    v->rng = 0x2545F491u;
}

static void head_seat(BeatsHead *h, int pos)
{
    h->pos = pos;
    h->dir = 1;
    h->cache_n = 0;
}

static void head_get(BeatsHead *h, void *sample, int length, int stereo,
                     BeatsReadFn read, float *l, float *r)
{
    int k = h->pos - h->cache_pos;
    if (h->cache_n == 0 || k < 0 || k >= h->cache_n) {
        /* Reverse heads cache the samples behind them. */
        int i, at = h->dir < 0 ? h->pos - (BEATS_CACHE - 1) : h->pos;
        read_wrap(read, sample, length, h->cl, at, BEATS_CACHE, 0);
        if (stereo)
            read_wrap(read, sample, length, h->cr, at, BEATS_CACHE, 1);
        else
            for (i = 0; i < BEATS_CACHE; i++)
                h->cr[i] = h->cl[i];
        h->cache_pos = at;
        h->cache_n = BEATS_CACHE;
        k = h->pos - at;
    }
    *l = h->cl[k];
    *r = h->cr[k];
}

static float rnd01(BeatsVoice *v)
{
    v->rng ^= v->rng << 13;
    v->rng ^= v->rng >> 17;
    v->rng ^= v->rng << 5;
    return (float)(v->rng >> 8) * (1.f / 16777216.f);
}

/* One sample of the granular fill for the segment [start, end). */
static void cloud_sample(BeatsVoice *v, void *sample, int length, int stereo, BeatsReadFn read,
                         int start, int end, float *l, float *r)
{
    float sl = 0.f, sr = 0.f;
    int k;
    if (--v->spawn_in <= 0) {
        int len = (int)((float)v->grain_len * (0.75f + 0.5f * rnd01(v)));
        int lo = end - v->pp_span, hi;
        if (len < 64)
            len = 64;
        if (lo < start)
            lo = start;
        hi = end - len;
        if (hi < lo)
            hi = lo = end - len > start ? end - len : start;
        for (k = 0; k < BEATS_CLOUD; k++)
            if (!v->cloud[k].active)
                break;
        if (k < BEATS_CLOUD) {
            BeatsCloudGrain *c = &v->cloud[k];
            c->active = 1;
            c->age = 0;
            c->len = len;
            head_seat(&c->h, lo + (int)(rnd01(v) * (float)(hi - lo)));
        }
        v->spawn_in = len / (v->density > 0 ? v->density : 1);
        if (v->spawn_in < 1)
            v->spawn_in = 1;
    }
    for (k = 0; k < BEATS_CLOUD; k++) {
        BeatsCloudGrain *c = &v->cloud[k];
        float t, e, cl, cr;
        if (!c->active)
            continue;
        t = (float)c->age / (float)c->len;
        e = 4.f * t * (1.f - t); /* parabolic window */
        head_get(&c->h, sample, length, stereo, read, &cl, &cr);
        sl += cl * e;
        sr += cr * e;
        c->h.pos++;
        if (++c->age >= c->len)
            c->active = 0;
    }
    /* A parabola averages 2/3: keep the cloud near the source level. */
    *l = sl * 1.5f / (float)(v->density > 0 ? v->density : 1);
    *r = sr * 1.5f / (float)(v->density > 0 ? v->density : 1);
}

/* Tail loop length for a segment [start, end): the lag L whose preceding
 * window best matches the window before the end (normalized correlation on
 * L+R), so jumping back by L continues the waveform. 0 = segment too short. */
static int pick_loop(BeatsVoice *v, void *sample, int length, int stereo,
                     BeatsReadFn read, int start, int end)
{
    int seg = end - start, lo = seg / 8, hi = seg / 2, span, i, L, best = 0, c0, c1;
    float best_s = -2.f, *x = v->scratch;
    if (lo < BEATS_LOOP_MIN)
        lo = BEATS_LOOP_MIN;
    if (hi > BEATS_LOOP_MAX)
        hi = BEATS_LOOP_MAX;
    if (hi > seg - BEATS_MATCH - v->loop_xf)
        hi = seg - BEATS_MATCH - v->loop_xf;
    if (hi < lo)
        return 0;
    span = hi + BEATS_MATCH;
    /* x[span - 1] is the sample just before `end`. */
    read_wrap(read, sample, length, x, end - span, span, 0);
    if (stereo) {
        float tmp[BEATS_CACHE];
        for (i = 0; i < span; i += BEATS_CACHE) {
            int k, n = span - i < BEATS_CACHE ? span - i : BEATS_CACHE;
            read_wrap(read, sample, length, tmp, end - span + i, n, 1);
            for (k = 0; k < n; k++)
                x[i + k] += tmp[k];
        }
    }
    {
        const float *ref = x + span - BEATS_MATCH;
        float rr = 0.f;
        for (i = 0; i < BEATS_MATCH; i++)
            rr += ref[i] * ref[i];
        if (rr < 1e-9f)
            return (lo + hi) / 2; /* silence: any loop is seamless */
        for (int pass = 0; pass < 2; pass++) {
            int step = pass ? 1 : 8;
            c0 = pass ? best - 8 : lo;
            c1 = pass ? best + 8 : hi;
            if (c0 < lo)
                c0 = lo;
            if (c1 > hi)
                c1 = hi;
            for (L = c0; L <= c1; L += step) {
                const float *cand = ref - L;
                float xy = 0.f, yy = 0.f, s;
                for (i = 0; i < BEATS_MATCH; i++) {
                    xy += ref[i] * cand[i];
                    yy += cand[i] * cand[i];
                }
                s = yy > 1e-12f ? xy / __builtin_sqrtf(rr * yy) : -1.f;
                if (s > best_s) {
                    best_s = s;
                    best = L;
                }
            }
        }
    }
    return best;
}

static int seg_end(const int32_t *starts, int n_starts, int k, int length)
{
    return k + 1 < n_starts ? (int)starts[k + 1] : length;
}

static int seg_of(const int32_t *starts, int n_starts, float pos)
{
    int lo = 0, hi = n_starts - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if ((float)starts[mid] <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* Start segment k. The grain is pre-rolled by `attack` samples: it starts
 * reading `attack` before the segment start, so its fade-in finishes exactly
 * as the segment (usually a hit) arrives, on time and at full gain. */
static void start_grain(BeatsVoice *v, const int32_t *starts, int n_starts,
                        int length, int k, int launch)
{
    int slot = v->live ^ 1, at = launch ? (int)starts[k] : (int)starts[k] - v->attack;
    BeatsGrain *old = &v->g[v->live], *g = &v->g[slot];
    /* The old grain already plays exactly where this one starts (it ran to its
     * end on time): hand over without a fade, or the overlap doubles. */
    int seamless = old->active && old->xf < 0 && !old->gated && !old->cloud && old->fade_out == 0 &&
                   old->a.dir > 0 && wrap(old->a.pos, length) == wrap(at, length);
    if (seamless)
        old->active = 0;
    else if (old->active && v->cur != -1)
        old->fade_out = old->fade_out > 0 && old->fade_out < v->cut ? old->fade_out : v->cut;
    g->active = 1;
    g->seg = k;
    g->end = seg_end(starts, n_starts, k, length);
    g->age = launch || seamless ? v->attack : 0;
    g->fade_out = 0;
    g->loop_len = 0;
    g->xf = -1;
    g->gated = 0;
    g->pp_lo = 0;
    g->cloud = 0;
    head_seat(&g->a, at);
    v->live = slot;
    v->cur = k;
}

#define BEATS_TURN 256 /* crossfade at a ping-pong turn */

/* Crossfade onto head b, already seated; xf_len samples. */
static void begin_xf(BeatsGrain *g, int xf_len)
{
    g->b.cache_n = 0;
    g->xf = 0;
    g->xf_len = xf_len;
}

void beats_process(BeatsVoice *v, void *sample, int length, int stereo,
                   BeatsReadFn read, const int32_t *starts, int n_starts,
                   float *dst_l, float *dst_r, int n, float ratio, int reset)
{
    static const int32_t whole[1] = { 0 };
    int i, j;
    if (!v || !dst_l || n <= 0 || length <= 0 || !read)
        return;
    if (v->magic != BEATS_MAGIC)
        beats_init(v);
    if (!starts || n_starts <= 0) {
        starts = whole;
        n_starts = 1;
    }
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    if (reset) {
        int fill = v->fill, attack = v->attack, cut = v->cut, xf = v->loop_xf, pp = v->pp_span;
        int gl = v->grain_len, dens = v->density;
        beats_init(v);
        v->pp_span = pp;
        v->grain_len = gl;
        v->density = dens;
        v->fill = fill;
        v->attack = attack;
        v->cut = cut;
        v->loop_xf = xf;
    }

    for (i = 0; i < n; i++) {
        float yl = 0.f, yr = 0.f;
        /* The next segment starts `attack` output samples early (pre-roll), so
         * segments follow the host position `attack` samples ahead. */
        float p = (float)v->host_i + v->host_f + (float)v->attack * ratio;
        if (p >= (float)length)
            p -= (float)length;
        if (p < v->lead_prev)
            v->cur = -2; /* loop wrap: a new pass starts at segment 0 */
        v->lead_prev = p;
        if (v->cur < 0 || p >= (float)v->seg_hi || p < (float)v->seg_lo) {
            int k = seg_of(starts, n_starts, p);
            if (v->cur < 0 || k != v->cur)
                start_grain(v, starts, n_starts, length, k, v->cur == -1);
            v->seg_lo = (int)starts[k];
            v->seg_hi = seg_end(starts, n_starts, k, length);
        }

        for (j = 0; j < 2; j++) {
            BeatsGrain *g = &v->g[j];
            float al, ar, env;
            /* Output samples until the next segment takes over. */
            float due = ((float)g->end - p) / ratio;
            if (!g->active)
                continue;
            if (j == v->live && g->xf < 0 && !g->gated) {
                if (v->fill == BEATS_GRANULAR && !g->cloud &&
                    g->a.pos >= g->end - v->loop_xf &&
                    due > (float)(g->end - g->a.pos) + 1.f) {
                    /* Out of audio: hand over to a grain cloud over the tail. */
                    int k;
                    for (k = 0; k < BEATS_CLOUD; k++)
                        v->cloud[k].active = 0;
                    g->cloud = 1;
                    v->cloud_owner = j;
                    v->cloud_age = 0;
                    v->spawn_in = 0;
                } else if (v->fill == BEATS_PINGPONG && !g->pp_lo && g->a.dir > 0 &&
                    g->a.pos >= g->end - 1 && due > 1.f) {
                    /* Out of audio: bounce over the tail until the next segment. */
                    int start = (int)starts[g->seg], span = v->pp_span;
                    if (span > (g->end - start) / 2)
                        span = (g->end - start) / 2;
                    if (span < 2 * BEATS_TURN) {
                        g->gated = 1;
                        g->fade_out = v->loop_xf;
                    } else {
                        g->pp_lo = g->end - span;
                        g->b = g->a;
                        g->b.dir = -1;
                        begin_xf(g, BEATS_TURN);
                    }
                } else if (g->pp_lo && ((g->a.dir > 0 && g->a.pos >= g->end - 1) ||
                                        (g->a.dir < 0 && g->a.pos <= g->pp_lo))) {
                    /* Turn: crossfade into a head running the other way. */
                    g->b = g->a;
                    g->b.dir = -g->a.dir;
                    begin_xf(g, BEATS_TURN);
                    v->n_loops++;
                } else if ((v->fill == BEATS_LOOP || v->fill == BEATS_GATE) &&
                           g->a.pos >= g->end - v->loop_xf &&
                           due > (float)(g->end - g->a.pos) + 1.f) {
                    /* Out of audio before the next segment: loop the tail or gate. */
                    if (v->fill == BEATS_LOOP && g->loop_len == 0)
                        g->loop_len = pick_loop(v, sample, length, stereo, read,
                                                (int)starts[g->seg], g->end);
                    if (v->fill == BEATS_LOOP && g->loop_len > 0) {
                        int xf = v->loop_xf < g->loop_len / 2 ? v->loop_xf : g->loop_len / 2;
                        if (g->a.pos >= g->end - xf) {
                            head_seat(&g->b, g->a.pos - g->loop_len);
                            begin_xf(g, xf);
                            v->n_loops++;
                        }
                    } else {
                        g->gated = 1;
                        g->fade_out = v->loop_xf;
                    }
                }
            }
            head_get(&g->a, sample, length, stereo, read, &al, &ar);
            if (g->xf >= 0) {
                float bl, br, w = (float)(g->xf + 1) / (float)g->xf_len;
                head_get(&g->b, sample, length, stereo, read, &bl, &br);
                al = al * (1.f - w) + bl * w;
                ar = ar * (1.f - w) + br * w;
                g->b.pos += g->b.dir;
                if (++g->xf >= g->xf_len) {
                    g->a = g->b;
                    g->xf = -1;
                    g->a.pos -= g->a.dir; /* advanced below */
                }
            }
            if (g->cloud && v->cloud_owner == j) {
                /* Main head fades out over loop_xf while the cloud fades in. */
                float cl, cr, c = v->cloud_age < v->loop_xf
                                      ? (float)(v->cloud_age + 1) / (float)v->loop_xf : 1.f;
                cloud_sample(v, sample, length, stereo, read, (int)starts[g->seg], g->end, &cl, &cr);
                al = al * (1.f - c) + cl * c;
                ar = ar * (1.f - c) + cr * c;
                v->cloud_age++;
            }
            env = g->age < v->attack ? (float)(g->age + 1) / (float)v->attack : 1.f;
            if (g->fade_out > 0) {
                int len = g->gated ? v->loop_xf : v->cut;
                env *= (float)g->fade_out / (float)len;
                if (--g->fade_out == 0) {
                    g->active = 0;
                    if (v->cloud_owner == j)
                        v->cloud_owner = -1;
                }
            }
            yl += al * env;
            yr += ar * env;
            g->a.pos += g->a.dir;
            g->age++;
        }
        dst_l[i] += yl;
        if (dst_r)
            dst_r[i] += yr;

        v->host_f += ratio;
        while (v->host_f >= 1.f) {
            v->host_f -= 1.f;
            if (++v->host_i >= length)
                v->host_i = 0;
        }
    }
}

int beats_detect(void *sample, int length, int stereo, BeatsReadFn read,
                 int32_t *starts, int max_starts, float sensitivity, int min_gap)
{
    enum { HOP = 256 };
    float buf[HOP + 1], tmp[HOP], hist[4] = { 0.f, 0.f, 0.f, 0.f }, prev = 0.f;
    int n = 0, f, frames = length / HOP, last = 0;
    if (max_starts <= 0 || length <= 0)
        return 0;
    starts[n++] = 0;
    for (f = 0; f < frames && n < max_starts; f++) {
        int i, pos = f * HOP, at = -1;
        float e = 0.f, avg, peak = 0.f;
        read(sample, buf + 1, HOP, pos, 0);
        if (stereo) {
            read(sample, tmp, HOP, pos, 1);
            for (i = 0; i < HOP; i++)
                buf[i + 1] += tmp[i];
        }
        buf[0] = prev;
        prev = buf[HOP];
        for (i = 0; i < HOP; i++) {
            float d = buf[i + 1] - buf[i];
            e += d * d;
            if (fabs_(d) > peak)
                peak = fabs_(d);
        }
        avg = 0.25f * (hist[0] + hist[1] + hist[2] + hist[3]);
        hist[f & 3] = e;
        if (f < 4 || e < 1e-4f || e < sensitivity * avg || pos - last < min_gap)
            continue;
        /* Onset: first sample whose step reaches a quarter of the frame's peak,
         * backed off 32 samples. */
        for (i = 0; i < HOP; i++)
            if (fabs_(buf[i + 1] - buf[i]) >= 0.25f * peak) {
                at = pos + i - 32;
                break;
            }
        if (at <= last)
            at = last + 1;
        starts[n++] = at;
        last = at;
    }
    return n;
}

int beats_fill_gaps(const int32_t *slices, int n, int length, float step,
                    float max_gap, int32_t *out, int max_out)
{
    int i, m = 0;
    for (i = 0; i < n && m < max_out; i++) {
        float a = (float)slices[i], b = i + 1 < n ? (float)slices[i + 1] : (float)length;
        out[m++] = slices[i];
        if (max_gap > 0.f && step > 1.f && b - a > max_gap) {
            int k = (int)((a + 0.5f * step) / step) + 1;
            float g;
            if ((float)(k - 1) * step >= a + 0.5f * step)
                k--;
            for (g = (float)k * step; g <= b - 0.5f * step && m < max_out; g += step)
                out[m++] = (int32_t)(g + 0.5f);
        }
    }
    return m;
}
