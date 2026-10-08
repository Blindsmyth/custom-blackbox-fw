#include "smooth.h"
#include "hann_lut.h"

#include <stddef.h>

_Static_assert(offsetof(SmoothVoice, owner) == SMOOTH_OFF_OWNER, "owner offset");
_Static_assert(offsetof(SmoothVoice, reset_req) == SMOOTH_OFF_RESET, "reset offset");
_Static_assert(offsetof(SmoothVoice, block_done) == SMOOTH_OFF_BLOCK, "block offset");
_Static_assert(sizeof(SMOOTH_HANN) / sizeof(SMOOTH_HANN[0]) == SMOOTH_WIN, "hann size");

static float window_at(int i, int first)
{
    float h = SMOOTH_HANN[i];
    if (!first)
        return h;
    /* Fade the first grain in; keep Hann on the overlap half so OLA stays flat. */
    if (i < 64)
        return h * (float)(i + 1) * (1.f / 64.f);
    return h;
}

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

/* Coarse then refine over SMOOTH_MATCH only (not the full hop). */
static int seek_offset(const float *prev, const float *span)
{
    int best = 0;
    float best_score = 1.0e30f;
    int d, i, d0, d1;
    for (d = -SMOOTH_SEARCH; d <= SMOOTH_SEARCH; d += 16) {
        float score = 0.f;
        for (i = 0; i < SMOOTH_MATCH; i += 4) {
            float diff = prev[i] - span[SMOOTH_SEARCH + d + i];
            score += diff < 0.f ? -diff : diff;
        }
        if (score < best_score) {
            best_score = score;
            best = d;
        }
    }
    d0 = best - 16;
    d1 = best + 16;
    if (d0 < -SMOOTH_SEARCH)
        d0 = -SMOOTH_SEARCH;
    if (d1 > SMOOTH_SEARCH)
        d1 = SMOOTH_SEARCH;
    best_score = 1.0e30f;
    for (d = d0; d <= d1; d++) {
        float score = 0.f;
        for (i = 0; i < SMOOTH_MATCH; i++) {
            float diff = prev[i] - span[SMOOTH_SEARCH + d + i];
            score += diff < 0.f ? -diff : diff;
        }
        if (score < best_score) {
            best_score = score;
            best = d;
        }
    }
    return best;
}

static void shift_acc(SmoothVoice *v)
{
    int i;
    for (i = 0; i < SMOOTH_HOP; i++) {
        v->acc_l[i] = v->acc_l[i + SMOOTH_HOP];
        v->acc_r[i] = v->acc_r[i + SMOOTH_HOP];
    }
    fzero(v->acc_l + SMOOTH_HOP, SMOOTH_HOP);
    fzero(v->acc_r + SMOOTH_HOP, SMOOTH_HOP);
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
    /* On a transient, push the new grain in with a short crossfade instead of
     * ducking the whole hop (that read as a click every kick). */
    if (transient) {
        for (i = 0; i < 128 && i < SMOOTH_HOP; i++) {
            float keep = 1.f - (float)(i + 1) * (1.f / 128.f);
            v->acc_l[i] *= keep;
            v->acc_r[i] *= keep;
        }
    }
    for (i = 0; i < SMOOTH_HOP; i++)
        v->prev_l[i] = v->scratch[start + SMOOTH_HOP + i];
    for (i = 0; i < SMOOTH_WIN; i++) {
        float w = window_at(i, first);
        if (transient && i < 128)
            w = (float)(i + 1) * (1.f / 128.f);
        v->acc_l[i] += v->scratch[start + i] * w;
    }
    if (stereo) {
        read_span(read, sample, length, v->scratch, nominal + offset, SMOOTH_WIN, 1);
        for (i = 0; i < SMOOTH_WIN; i++) {
            float w = window_at(i, first);
            if (transient && i < 128)
                w = (float)(i + 1) * (1.f / 128.f);
            v->acc_r[i] += v->scratch[i] * w;
        }
    } else {
        for (i = 0; i < SMOOTH_WIN; i++) {
            float w = window_at(i, first);
            if (transient && i < 128)
                w = (float)(i + 1) * (1.f / 128.f);
            v->acc_r[i] += v->scratch[start + i] * w;
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
            if (v->have_grain && v->emitted >= SMOOTH_HOP)
                shift_acc(v);
            synthesize(v, sample, length, stereo, read, ratio);
            v->emitted = 0;
        }
        take = SMOOTH_HOP - v->emitted;
        if (take > n - produced)
            take = n - produced;
        for (i = 0; i < take; i++) {
            dst_l[produced + i] += v->acc_l[v->emitted + i];
            if (dst_r)
                dst_r[produced + i] += v->acc_r[v->emitted + i];
        }
        v->emitted += take;
        produced += take;
    }
}
