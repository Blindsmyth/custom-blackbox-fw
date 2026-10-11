#include "smooth.h"
#include "hann_lut.h"

#include <stddef.h>

_Static_assert(offsetof(SmoothVoice, owner) == SMOOTH_OFF_OWNER, "owner offset");
_Static_assert(offsetof(SmoothVoice, reset_req) == SMOOTH_OFF_RESET, "reset offset");
_Static_assert(offsetof(SmoothVoice, block_done) == SMOOTH_OFF_BLOCK, "block offset");
_Static_assert(sizeof(SMOOTH_HANN) / sizeof(SMOOTH_HANN[0]) == SMOOTH_WIN, "hann size");

static void fzero(float *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = 0.f;
}

void smooth_init(SmoothVoice *v)
{
    uint32_t *w = (uint32_t *)v;
    unsigned i;
    for (i = 0; i < sizeof(SmoothVoice) / 4u; i++)
        w[i] = 0;
    v->magic = SMOOTH_MAGIC;
}

static int wrap_pos(int pos, int length)
{
    if (length <= 0)
        return 0;
    while (pos >= length)
        pos -= length;
    while (pos < 0)
        pos += length;
    return pos;
}

static void read_span(SmoothReadFn read, void *sample, int length,
                      float *dest, int pos, int count, int chan)
{
    int i = 0;
    if (length <= 0 || count <= 0) {
        fzero(dest, count > 0 ? count : 0);
        return;
    }
    while (i < count) {
        int p, n, k;
        if (pos + i < 0) {
            n = -(pos + i);
            if (n > count - i)
                n = count - i;
            for (k = 0; k < n; k++)
                dest[i++] = 0.f;
            continue;
        }
        p = wrap_pos(pos + i, length);
        n = length - p;
        if (n > count - i)
            n = count - i;
        if (n < 1)
            n = 1;
        if (read)
            read(sample, dest + i, n, p, chan);
        else
            fzero(dest + i, n);
        i += n;
    }
}

static float energy(const float *p, int n)
{
    float e = 0.f;
    int i;
    for (i = 0; i < n; i += 4) {
        float s = p[i];
        e += s * s;
    }
    return e;
}

/* SAD of prev against span at d, every STEPth sample, 4 lanes so the FPU
 * adds overlap. Bails once the partial sum reaches `limit`: abs diffs only
 * grow it, so a candidate that has hit the best score cannot beat it. */
#define SAD_BODY(STEP)                                                     \
    const float *p = prev, *q = span + SMOOTH_SEARCH + d;                  \
    const float *end = prev + SMOOTH_MATCH;                                \
    float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;                          \
    while (p < end) {                                                      \
        const float *stop = p + 16 * (STEP);                               \
        do {                                                               \
            s0 += __builtin_fabsf(p[0] - q[0]);                            \
            s1 += __builtin_fabsf(p[STEP] - q[STEP]);                      \
            s2 += __builtin_fabsf(p[2 * (STEP)] - q[2 * (STEP)]);          \
            s3 += __builtin_fabsf(p[3 * (STEP)] - q[3 * (STEP)]);          \
            p += 4 * (STEP);                                               \
            q += 4 * (STEP);                                               \
        } while (p < stop);                                                \
        if (s0 + s1 + s2 + s3 >= limit)                                    \
            break;                                                         \
    }                                                                      \
    return s0 + s1 + s2 + s3;

static float sad_coarse(const float *prev, const float *span, int d, float limit)
{
    SAD_BODY(4)
}

static float sad_fine(const float *prev, const float *span, int d, float limit)
{
    SAD_BODY(1)
}

static int seek_range(const float *prev, const float *span, int best, int reach, int step)
{
    float best_score = 1.0e30f;
    int d, d0 = best - reach, d1 = best + reach;
    if (d0 < -SMOOTH_SEARCH)
        d0 = -SMOOTH_SEARCH;
    if (d1 > SMOOTH_SEARCH)
        d1 = SMOOTH_SEARCH;
    for (d = d0; d <= d1; d += step) {
        float score = step > 4 ? sad_coarse(prev, span, d, best_score)
                               : sad_fine(prev, span, d, best_score);
        if (score < best_score) {
            best_score = score;
            best = d;
        }
    }
    return best;
}

/* Coarse (16) on every 4th sample, then 4 and 1 over SMOOTH_MATCH. */
static int seek_offset(const float *prev, const float *span)
{
    int best = seek_range(prev, span, 0, SMOOTH_SEARCH, 16);
    best = seek_range(prev, span, best, 12, 4);
    return seek_range(prev, span, best, 3, 1);
}

/* out = tail + first half of the grain; tail = second half. Same sums as a
 * 2048 accumulator that shifts by a hop, without the shift or the zeroing. */
static void overlap_add(float *out, float *tail, const float *src, int first, int transient)
{
    int i = 0;
    if (first) {
        /* Fade the first grain in; keep Hann on the overlap half so OLA stays flat. */
        for (; i < 64; i++) {
            float w = SMOOTH_HANN[i] * (float)(i + 1) * (1.f / 64.f);
            out[i] = tail[i] + src[i] * w;
        }
    } else if (transient) {
        /* Push the new grain in with a short crossfade instead of ducking the
         * whole hop (that read as a click every kick). */
        for (; i < 128; i++) {
            float keep = 1.f - (float)(i + 1) * (1.f / 128.f);
            float w = (float)(i + 1) * (1.f / 128.f);
            out[i] = tail[i] * keep + src[i] * w;
        }
    }
    {
        const float *w = SMOOTH_HANN + i, *x = src + i, *t = tail + i;
        float *o = out + i, *end = out + SMOOTH_HOP;
        while (o < end)
            *o++ = *t++ + *x++ * *w++;
    }
    {
        const float *w = SMOOTH_HANN + SMOOTH_HOP, *x = src + SMOOTH_HOP;
        float *t = tail, *end = tail + SMOOTH_HOP;
        while (t < end)
            *t++ = *x++ * *w++;
    }
}

static void synthesize(SmoothVoice *v, void *sample, int length, int stereo,
                       SmoothReadFn read, float ratio)
{
    int nominal, origin, offset, start, first, transient, i;
    float e0, e1;
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    nominal = (int)v->in_pos;
    if (nominal < 0)
        nominal = 0;
    origin = nominal - SMOOTH_SEARCH;
    read_span(read, sample, length, v->scratch, origin, SMOOTH_SPAN, 0);
    offset = 0;
    transient = 0;
    first = !v->have_grain;
    if (!first) {
        e0 = energy(v->scratch + (SMOOTH_SEARCH - 256), 256);
        e1 = energy(v->scratch + SMOOTH_SEARCH, 256);
        if (e1 > e0 * 2.5f && e1 > 4.f) {
            transient = 1;
            offset = 0;
        } else {
            offset = seek_offset(v->prev_l, v->scratch);
        }
    }
    start = SMOOTH_SEARCH + offset;
    if (start < 0)
        start = 0;
    if (start > SMOOTH_SPAN - SMOOTH_WIN)
        start = SMOOTH_SPAN - SMOOTH_WIN;
    for (i = 0; i < SMOOTH_HOP; i++)
        v->prev_l[i] = v->scratch[start + SMOOTH_HOP + i];
    overlap_add(v->out_l, v->tail_l, v->scratch + start, first, transient);
    if (stereo) {
        read_span(read, sample, length, v->scratch, nominal + offset, SMOOTH_WIN, 1);
        overlap_add(v->out_r, v->tail_r, v->scratch, first, transient);
    } else {
        for (i = 0; i < SMOOTH_HOP; i++) {
            v->out_r[i] = v->out_l[i];
            v->tail_r[i] = v->tail_l[i];
        }
    }
    v->have_grain = 1;
    v->in_pos += (float)SMOOTH_HOP * ratio;
    if (length > 0) {
        if (v->in_pos >= (float)length * 8.f)
            v->in_pos = 0.f;
        while (v->in_pos >= (float)length)
            v->in_pos -= (float)length;
    }
}

void smooth_process(SmoothVoice *v, void *sample, int length, int stereo,
                    SmoothReadFn read, float *dst_l, float *dst_r,
                    int n, float ratio, int reset)
{
    int produced = 0;
    if (!v || !dst_l || n <= 0 || length <= 0)
        return;
    if (v->magic != SMOOTH_MAGIC)
        smooth_init(v);
    if (reset) {
        uint32_t owner = v->owner;
        int32_t block = v->block_done;
        int32_t prev = v->prev_active;
        smooth_init(v);
        v->owner = owner;
        v->block_done = block;
        v->prev_active = prev;
    }
    while (produced < n) {
        int take, i;
        if (!v->have_grain || v->emitted >= SMOOTH_HOP) {
            synthesize(v, sample, length, stereo, read, ratio);
            v->emitted = 0;
        }
        take = SMOOTH_HOP - v->emitted;
        if (take > n - produced)
            take = n - produced;
        {
            const float *sl = v->out_l + v->emitted;
            const float *sr = v->out_r + v->emitted;
            float *ol = dst_l + produced;
            if (dst_r) {
                float *or_ = dst_r + produced;
                for (i = 0; i < take; i++) {
                    ol[i] += sl[i];
                    or_[i] += sr[i];
                }
            } else {
                for (i = 0; i < take; i++)
                    ol[i] += sl[i];
            }
        }
        v->emitted += take;
        produced += take;
    }
}
