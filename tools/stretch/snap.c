#include "snap.h"

#include <stddef.h>

_Static_assert(offsetof(SnapVoice, owner) == SMOOTH_OFF_OWNER, "owner offset");
_Static_assert(offsetof(SnapVoice, reset_req) == SMOOTH_OFF_RESET, "reset offset");
_Static_assert(offsetof(SnapVoice, block_done) == SMOOTH_OFF_BLOCK, "block offset");

static void fzero(float *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = 0.f;
}

void snap_init(SnapVoice *v)
{
    uint32_t *w = (uint32_t *)v;
    unsigned i;
    for (i = 0; i < sizeof(SnapVoice) / 4u; i++)
        w[i] = 0;
    v->magic = SNAP_MAGIC;
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

static void read_n(SnapReadFn read, void *sample, int length,
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

/* Lowest index with slices[i] >= pos; if none, wrap to 0. */
static int find_next_slice(const uint32_t *slices, int n, int pos)
{
    int lo = 0, hi = n - 1, best = -1;
    if (!slices || n <= 0)
        return -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if ((int)slices[mid] < pos) {
            lo = mid + 1;
        } else {
            best = mid;
            hi = mid - 1;
        }
    }
    return best >= 0 ? best : 0;
}

/* Coarse then refine. prev_tail is the last SNAP_MATCH samples of the previous
 * raw grain. Align the new grain start to that period so stretch does not
 * read as pitch tracking tempo on bass. */
static int period_seek(const float *prev_tail, const float *span)
{
    int best = 0, d, i, d0, d1;
    float best_score = 1.0e30f;
    for (d = -SNAP_SEARCH; d <= SNAP_SEARCH; d += 8) {
        float score = 0.f;
        for (i = 0; i < SNAP_MATCH; i += 4) {
            float diff = prev_tail[i] - span[SNAP_SEARCH + d + i];
            score += diff < 0.f ? -diff : diff;
        }
        if (score < best_score) {
            best_score = score;
            best = d;
        }
    }
    d0 = best - 8;
    d1 = best + 8;
    if (d0 < -SNAP_SEARCH)
        d0 = -SNAP_SEARCH;
    if (d1 > SNAP_SEARCH)
        d1 = SNAP_SEARCH;
    best_score = 1.0e30f;
    for (d = d0; d <= d1; d++) {
        float score = 0.f;
        for (i = 0; i < SNAP_MATCH; i++) {
            float diff = prev_tail[i] - span[SNAP_SEARCH + d + i];
            score += diff < 0.f ? -diff : diff;
        }
        if (score < best_score) {
            best_score = score;
            best = d;
        }
    }
    return best;
}

static void synthesize(SnapVoice *v, void *sample, int length, int stereo,
                       SnapReadFn read, const uint32_t *slices, int n_slices,
                       float ratio)
{
    int nominal, target, si, off, i, had;
    float *span = v->scratch;

    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;

    nominal = (int)v->in_pos;
    if (nominal < 0)
        nominal = 0;
    target = wrap_pos(nominal, length);

    si = find_next_slice(slices, n_slices, nominal);
    if (si >= 0) {
        int s = (int)slices[si];
        int dist = s - nominal;
        if (dist < 0)
            dist += length;
        if (dist <= SNAP_NEAR) {
            target = wrap_pos(s, length);
            v->slice_i = si;
        }
    }

    off = 0;
    if (v->have && length > SNAP_MATCH + 2 * SNAP_SEARCH) {
        read_n(read, sample, length, span, target - SNAP_SEARCH,
               SNAP_MATCH + 2 * SNAP_SEARCH, 0);
        off = period_seek(v->prev_l + (SNAP_HOP - SNAP_MATCH), span);
        target = wrap_pos(target + off, length);
    }

    read_n(read, sample, length, v->grain_l, target, SNAP_HOP, 0);
    if (stereo)
        read_n(read, sample, length, v->grain_r, target, SNAP_HOP, 1);
    else {
        for (i = 0; i < SNAP_HOP; i++)
            v->grain_r[i] = v->grain_l[i];
    }

    had = v->have;
    /* Apply XF from previous raw tail into the new head, while copying the
     * new raw grain into prev for the next hop (no large stack temps). */
    for (i = 0; i < SNAP_HOP; i++) {
        float nl = v->grain_l[i];
        float nr = v->grain_r[i];
        if (had && i < SNAP_XF) {
            float t = (float)(i + 1) / (float)SNAP_XF;
            float w_old = 1.f - t;
            w_old = w_old * w_old * (3.f - 2.f * w_old);
            float w_new = 1.f - w_old;
            int pi = SNAP_HOP - SNAP_XF + i;
            v->grain_l[i] = v->prev_l[pi] * w_old + nl * w_new;
            v->grain_r[i] = v->prev_r[pi] * w_old + nr * w_new;
        }
        v->prev_l[i] = nl;
        v->prev_r[i] = nr;
    }

    v->have = 1;
    v->primed = 0;
    v->in_pos += (float)SNAP_HOP * ratio;
    if (length > 0) {
        if (v->in_pos >= (float)length * 8.f)
            v->in_pos = 0.f;
        while (v->in_pos >= (float)length)
            v->in_pos -= (float)length;
    }
}

void snap_process(SnapVoice *v, void *sample, int length, int stereo,
                  SnapReadFn read, const uint32_t *slices, int n_slices,
                  float *dst_l, float *dst_r, int n, float ratio, int reset)
{
    int produced = 0;

    if (!v || !dst_l || n <= 0 || length <= 0)
        return;
    if (v->magic != SNAP_MAGIC)
        snap_init(v);
    if (reset) {
        uint32_t owner = v->owner;
        int32_t block = v->block_done;
        int32_t prev = v->prev_active;
        snap_init(v);
        v->owner = owner;
        v->block_done = block;
        v->prev_active = prev;
    }

    while (produced < n) {
        int take, i;
        if (!v->have || v->primed >= SNAP_HOP)
            synthesize(v, sample, length, stereo, read, slices, n_slices, ratio);

        take = SNAP_HOP - v->primed;
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
