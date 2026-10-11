#include "vari.h"

#include <stddef.h>

_Static_assert(offsetof(VariVoice, owner) == SMOOTH_OFF_OWNER, "owner offset");
_Static_assert(offsetof(VariVoice, reset_req) == SMOOTH_OFF_RESET, "reset offset");
_Static_assert(offsetof(VariVoice, block_done) == SMOOTH_OFF_BLOCK, "block offset");

static void fzero(float *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = 0.f;
}

void vari_init(VariVoice *v)
{
    uint32_t *w = (uint32_t *)v;
    unsigned i;
    for (i = 0; i < sizeof(VariVoice) / 4u; i++)
        w[i] = 0;
    v->magic = VARI_MAGIC;
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

static void read_n(VariReadFn read, void *sample, int length,
                   float *dest, int pos, int count, int chan)
{
    int i = 0;
    if (length <= 0 || count <= 0) {
        fzero(dest, count > 0 ? count : 0);
        return;
    }
    while (i < count) {
        int p = wrap_pos(pos + i, length);
        int n = length - p;
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

static float frame_energy(const float *p, int n)
{
    float e = 0.f;
    int i;
    for (i = 0; i < n; i++)
        e += p[i] * p[i];
    return e;
}

/* AMDF period in [VARI_MIN_PERIOD, VARI_MAX_PERIOD]. 0 = unvoiced/silent. */
static int amdf_period(const float *x, int n)
{
    int tau, best = VARI_FALLBACK_PER;
    float best_score = 1.0e30f, e, thresh;
    if (n < VARI_MAX_PERIOD + 16)
        return 0;
    e = frame_energy(x, n);
    thresh = e * (1.e-4f);
    if (e < 1.0e-8f)
        return 0;
    for (tau = VARI_MIN_PERIOD; tau <= VARI_MAX_PERIOD; tau++) {
        float score = 0.f;
        int i, lim = n - tau;
        if (lim > 128)
            lim = 128;
        for (i = 0; i < lim; i++) {
            float d = x[i] - x[i + tau];
            score += d < 0.f ? -d : d;
        }
        score /= (float)lim;
        if (score < best_score) {
            best_score = score;
            best = tau;
        }
    }
    /* Reject weak periodicity (noise / unvoiced). */
    if (best_score > 0.35f * (e / (float)n + 1.0e-6f) && best_score > thresh * 50.f)
        return 0;
    return best;
}

static int push_mark(uint32_t *marks, int n, int max, uint32_t pos)
{
    int min_gap = VARI_MIN_PERIOD / 4;
    if (n <= 0) {
        if (max < 1)
            return 0;
        marks[0] = pos;
        return 1;
    }
    if ((int)pos < (int)marks[n - 1] + min_gap)
        return n;
    if (n >= max)
        return n;
    marks[n] = pos;
    return n + 1;
}

void vari_analyze_begin(VariAnalyze *a)
{
    if (!a)
        return;
    a->pos = 0;
    a->next_epoch = 0;
    a->period = VARI_FALLBACK_PER;
    a->n_marks = 0;
    a->done = 0;
    a->prev_e = 0.f;
}

int vari_analyze_step(VariAnalyze *a, void *sample, int length, int stereo,
                      VariReadFn read, uint32_t *marks, int max_marks,
                      float *scratch, int scratch_n, int budget)
{
    int span_n = VARI_SCRATCH_N;
    int steps;
    (void)stereo;

    if (!a || !marks || max_marks < 2 || !read || !scratch || scratch_n < span_n)
        return 1;
    if (length <= span_n) {
        a->n_marks = push_mark(marks, 0, max_marks, 0);
        if (length > 1)
            a->n_marks = push_mark(marks, a->n_marks, max_marks, (uint32_t)(length - 1));
        a->done = 1;
        return 1;
    }
    if (a->done)
        return 1;
    if (a->n_marks == 0)
        a->n_marks = push_mark(marks, 0, max_marks, 0);
    if (budget < 1)
        budget = 1;

    for (steps = 0; steps < budget && a->n_marks < max_marks; steps++) {
        float e;
        int p, onset;

        if (a->pos + span_n > length)
            break;

        read_n(read, sample, length, scratch, a->pos, span_n, 0);
        e = frame_energy(scratch, VARI_FRAME);
        p = amdf_period(scratch, span_n);
        if (p > 0)
            a->period = p;

        onset = (a->prev_e > 1.0e-8f && e > a->prev_e * 3.5f && e > 0.02f);
        if (onset)
            a->n_marks = push_mark(marks, a->n_marks, max_marks, (uint32_t)a->pos);

        while (a->next_epoch <= a->pos && a->n_marks < max_marks) {
            if (a->next_epoch > 0)
                a->n_marks = push_mark(marks, a->n_marks, max_marks,
                                       (uint32_t)a->next_epoch);
            a->next_epoch += a->period > 0 ? a->period : VARI_FALLBACK_PER;
        }
        a->prev_e = e * 0.7f + a->prev_e * 0.3f;
        a->pos += VARI_FRAME_HOP;
    }

    if (a->pos + span_n > length || a->n_marks >= max_marks) {
        while (a->next_epoch < length && a->n_marks < max_marks) {
            a->n_marks = push_mark(marks, a->n_marks, max_marks,
                                   (uint32_t)a->next_epoch);
            a->next_epoch += a->period > 0 ? a->period : VARI_FALLBACK_PER;
        }
        if (a->n_marks < max_marks &&
            (a->n_marks == 0 ||
             (int)marks[a->n_marks - 1] < length - VARI_MIN_PERIOD / 2))
            a->n_marks = push_mark(marks, a->n_marks, max_marks,
                                   (uint32_t)(length > 1 ? length - 1 : 0));
        a->done = 1;
        return 1;
    }
    return 0;
}

int vari_analyze(void *sample, int length, int stereo, VariReadFn read,
                 uint32_t *marks, int max_marks, float *scratch, int scratch_n)
{
    VariAnalyze a;
    vari_analyze_begin(&a);
    while (!vari_analyze_step(&a, sample, length, stereo, read, marks, max_marks,
                              scratch, scratch_n, 64))
        ;
    return a.n_marks;
}

static int find_nearest_mark(const uint32_t *marks, int n, int pos)
{
    int lo = 0, hi = n - 1, mid;
    if (!marks || n <= 0)
        return -1;
    if (pos <= (int)marks[0])
        return 0;
    if (pos >= (int)marks[n - 1])
        return n - 1;
    while (lo <= hi) {
        mid = (lo + hi) >> 1;
        if ((int)marks[mid] < pos)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    /* lo is first mark >= pos; hi is last < pos. */
    if (lo >= n)
        return n - 1;
    if (hi < 0)
        return 0;
    if ((int)marks[lo] - pos < pos - (int)marks[hi])
        return lo;
    return hi;
}

static int local_period(const uint32_t *marks, int n, int mi, int length)
{
    int p = VARI_FALLBACK_PER;
    if (marks && n >= 2 && mi >= 0) {
        if (mi + 1 < n)
            p = (int)marks[mi + 1] - (int)marks[mi];
        else if (mi > 0)
            p = (int)marks[mi] - (int)marks[mi - 1];
    }
    if (p < VARI_MIN_PERIOD)
        p = VARI_MIN_PERIOD;
    if (p > VARI_MAX_PERIOD)
        p = VARI_MAX_PERIOD;
    if (p > length / 2 && length > 0)
        p = length / 2;
    if (p < 16)
        p = 16;
    return p;
}

static void synthesize(VariVoice *v, void *sample, int length, int stereo,
                       VariReadFn read, const uint32_t *marks, int n_marks,
                       float ratio)
{
    int nominal, mi, mark, period, start, i, had, half;

    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;

    nominal = (int)v->in_pos;
    if (nominal < 0)
        nominal = 0;

    mi = find_nearest_mark(marks, n_marks, nominal);
    if (mi < 0) {
        mark = wrap_pos(nominal, length);
        period = VARI_FALLBACK_PER;
        v->mark_i = 0;
    } else {
        mark = wrap_pos((int)marks[mi], length);
        period = local_period(marks, n_marks, mi, length);
        v->mark_i = mi;
    }

    /* Period-centered hop: epoch in the middle of the local 2-period window,
     * then Snap-style XF into the previous hop (VariPhrase splice). */
    half = period;
    if (half > VARI_HOP / 2)
        half = VARI_HOP / 2;
    start = mark - half;
    read_n(read, sample, length, v->grain_l, start, VARI_HOP, 0);
    if (stereo)
        read_n(read, sample, length, v->grain_r, start, VARI_HOP, 1);
    else {
        for (i = 0; i < VARI_HOP; i++)
            v->grain_r[i] = v->grain_l[i];
    }

    had = v->have;
    for (i = 0; i < VARI_HOP; i++) {
        float nl = v->grain_l[i];
        float nr = v->grain_r[i];
        if (had && i < VARI_XF) {
            float t = (float)(i + 1) / (float)VARI_XF;
            float w_old = 1.f - t;
            w_old = w_old * w_old * (3.f - 2.f * w_old);
            float w_new = 1.f - w_old;
            int pi = VARI_HOP - VARI_XF + i;
            v->grain_l[i] = v->prev_l[pi] * w_old + nl * w_new;
            v->grain_r[i] = v->prev_r[pi] * w_old + nr * w_new;
        }
        v->prev_l[i] = nl;
        v->prev_r[i] = nr;
    }

    v->have = 1;
    v->primed = 0;
    v->grain_len = VARI_HOP;
    /* Same tempo convention as Smooth/Snap: ratio > 1 consumes input faster. */
    v->in_pos += (float)VARI_HOP * ratio;
    if (length > 0) {
        if (v->in_pos >= (float)length * 8.f)
            v->in_pos = 0.f;
        while (v->in_pos >= (float)length)
            v->in_pos -= (float)length;
    }
}

void vari_process(VariVoice *v, void *sample, int length, int stereo,
                  VariReadFn read, const uint32_t *marks, int n_marks,
                  float *dst_l, float *dst_r, int n, float ratio, int reset)
{
    int produced = 0;

    if (!v || !dst_l || n <= 0 || length <= 0)
        return;
    if (v->magic != VARI_MAGIC)
        vari_init(v);
    if (reset) {
        uint32_t owner = v->owner;
        int32_t block = v->block_done;
        int32_t prev = v->prev_active;
        vari_init(v);
        v->owner = owner;
        v->block_done = block;
        v->prev_active = prev;
    }

    while (produced < n) {
        int take, i;
        int glen = v->grain_len > 0 ? v->grain_len : VARI_HOP;
        if (!v->have || v->primed >= glen)
            synthesize(v, sample, length, stereo, read, marks, n_marks, ratio);
        glen = v->grain_len > 0 ? v->grain_len : VARI_HOP;

        take = glen - v->primed;
        if (take > n - produced)
            take = n - produced;
        for (i = 0; i < take; i++) {
            dst_l[produced + i] += v->grain_l[v->primed + i];
            if (dst_r)
                dst_r[produced + i] += v->grain_r[v->primed + i];
        }
        v->primed += take;
        produced += take;
    }
}
