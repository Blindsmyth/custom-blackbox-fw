#include "deluge.h"

/* Deluge tables (time_stretcher.cpp), by stretch ratio. Fine: 17 entries from
 * -1 to +1 octave in 1/8-octave steps. Coarse: -2..+2 octaves. Hop sizes in
 * 44.1 kHz samples, crossfade proportional in /256 of the hop, absolute x4
 * samples, random in /128 of the minimum hop. */
static const int16_t min_hop_coarse[] = { 2500, 3000, 3000, 600, 300 };
static const int16_t min_hop_fine[] = { 3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000,
                                        3000, 2500, 2000, 1500, 1000, 900, 800, 700, 600 };
static const int16_t max_hop_coarse[] = { 5000, 6500, 11000, 4000, 2500 };
static const int16_t max_hop_fine[] = { 6500, 7000, 8000, 9000, 9500, 9750, 10000, 11000,
                                        11000, 7500, 8000, 6500, 5000, 4750, 4500, 4250, 4000 };
static const int16_t xf_prop_coarse[] = { 200, 160, 0, 9, 9 };
static const int16_t xf_prop_fine[] = { 160, 140, 125, 110, 90, 70, 50, 20,
                                        0, 20, 20, 20, 20, 17, 14, 11, 9 };
static const int16_t xf_abs_coarse[] = { 10, 10, 60, 40, 20 };
static const int16_t xf_abs_fine[] = { 10, 10, 10, 10, 10, 10, 10, 170,
                                       60, 90, 20, 30, 40, 40, 40, 40, 40 };
static const int16_t random_coarse[] = { 85, 120, 0, 0, 0 };
static const int16_t random_fine[] = { 120, 95, 70, 45, 20, 15, 10, 10,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define SR_SCALE (48000.f / 44100.f)

#ifdef DELUGE_SHARED_SCRATCH
#define SCRATCH_OLD(v) ((v)->scratch + DELUGE_SCRATCH)
#else
#define SCRATCH_OLD(v) ((v)->scratch_old)
#endif

static float fabs_(float x)
{
    return x < 0.f ? -x : x;
}

/* log2 without libm (good to ~1e-4). */
static float log2_(float x)
{
    union { float f; uint32_t u; } b = { x };
    int e = (int)((b.u >> 23) & 255) - 127;
    float m;
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u;
    m = b.f;
    return (float)e + (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

static float tanh_(float x)
{
    float x2;
    if (x > 4.f)
        return 1.f;
    x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

static int wrap(int p, int length)
{
    p %= length;
    return p < 0 ? p + length : p;
}

static void read_wrap(DelugeReadFn read, void *sample, int length, float *dest,
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

/* L+R into dest. */
static void read_mono(DelugeVoice *v, DelugeReadFn read, void *sample, int length, int stereo,
                      float *dest, int pos, int count)
{
    int i, k;
    read_wrap(read, sample, length, dest, pos, count, 0);
    if (!stereo)
        return;
    for (i = 0; i < count; i += DELUGE_CACHE) {
        int n = count - i < DELUGE_CACHE ? count - i : DELUGE_CACHE;
        read_wrap(read, sample, length, v->newer.cr, pos + i, n, 1);
        for (k = 0; k < n; k++)
            dest[i + k] += v->newer.cr[k];
    }
    v->newer.cache_n = 0; /* borrowed its cache */
}

void deluge_init(DelugeVoice *v)
{
    unsigned char *w = (unsigned char *)v;
    unsigned i;
#ifdef DELUGE_SHARED_SCRATCH
    float *scratch = v->scratch;
#endif
    for (i = 0; i < sizeof(DelugeVoice); i++)
        w[i] = 0;
    v->magic = DELUGE_MAGIC;
    v->noise = 0x9E3779B9u;
#ifdef DELUGE_SHARED_SCRATCH
    v->scratch = scratch;
#endif
}

void deluge_perc(void *sample, int length, int stereo, DelugeReadFn read,
                 uint8_t *perc, int n_pix)
{
    float buf[256], tmp[256], prev = 0.f, pole[2] = { 0.f, 0.f }, last = 0.f;
    int pos, i;
    for (i = 0; i < n_pix; i++)
        perc[i] = 0;
    for (pos = 0; pos < length; pos += 256) {
        int n = length - pos < 256 ? length - pos : 256;
        read(sample, buf, n, pos, 0);
        if (stereo) {
            read(sample, tmp, n, pos, 1);
            for (i = 0; i < n; i++)
                buf[i] += tmp[i];
        }
        for (i = 0; i < n; i++) {
            float angle = fabs_(buf[i] - prev);
            int p = pos + i;
            prev = buf[i];
            pole[0] += (angle - pole[0]) * (1.f / 512.f);
            pole[1] += (pole[0] - pole[1]) * (1.f / 512.f);
            angle = pole[1];
            /* Sample at each pixel centre: relative jump of the smoothed slope. */
            if ((p & (DELUGE_PIX - 1)) == DELUGE_PIX / 2 && (p >> DELUGE_PIX_SHIFT) < n_pix) {
                float d = fabs_(angle - last), q = angle > 1e-12f ? tanh_(512.f * d / angle) : 0.f;
                perc[p >> DELUGE_PIX_SHIFT] = (uint8_t)(q * 255.f + 0.5f);
            }
            last = angle;
        }
    }
}

#ifdef DELUGE_PERC_RING
/* deluge_perc as the play position moves on: pixels from DELUGE_PERC_BEHIND behind it to
 * DELUGE_PERC_AHEAD ahead, at most `budget` samples smoothed per call (a new start catches up over a
 * few calls). The arithmetic is deluge_perc's, sample for sample: a window that starts at the file's
 * start, or runs on through its end, holds the same bytes; one started elsewhere smooths
 * DELUGE_PERC_WARM samples first (the smoothing forgets its start to well under a byte by then). */
static void perc_step(DelugeVoice *v, void *sample, int length, int stereo, DelugeReadFn read, int budget)
{
    DelugePerc *q = &v->perc;
    int n_pix = (length >> DELUGE_PIX_SHIFT) + 1, hp = v->host_i >> DELUGE_PIX_SHIFT, uh = 0;
    float *buf = v->scratch, *tmp = v->scratch + 256;
    if (q->length != length) {
        q->length = length;
        q->next = 0;
    }
    if (q->next > 0) {
        /* The play position counted on from the last step, the short way round the file: the window
         * keeps moving lap after lap (Clip Deck's short virtual file changes as it plays). */
        int d = wrap(hp - q->at, n_pix);
        if (d >= (n_pix + 1) / 2)
            d -= n_pix;
        uh = q->at + d;
        if (uh < q->lo || uh >= q->next + DELUGE_PERC_JUMP)
            q->next = 0;                    /* a jump: start again */
    }
    if (q->next <= 0) {
        /* From BEHIND pixels back; at the file's start from pixel 0 (exact, no warm-up). */
        int sp = hp - DELUGE_PERC_BEHIND, s0, w;
        if (sp < 0)
            sp = 0;
        s0 = sp << DELUGE_PIX_SHIFT;
        w = s0 < DELUGE_PERC_WARM ? s0 : DELUGE_PERC_WARM;
        q->next = q->lo = sp + n_pix;       /* > 0, and = sp mod n_pix */
        q->src = s0 - w;
        q->warm = w;
        q->prev = q->pole0 = q->pole1 = q->last = 0.f;
        q->cur = 0;
        uh = hp + n_pix;
    }
    q->at = uh;
    while (q->next < uh + DELUGE_PERC_AHEAD && budget > 0) {
        int n = length - q->src, i;
        if (n > 256)
            n = 256;
        if (n > budget)
            n = budget;
        read(sample, buf, n, q->src, 0);
        if (stereo) {
            read(sample, tmp, n, q->src, 1);
            for (i = 0; i < n; i++)
                buf[i] += tmp[i];
        }
        budget -= n;
        for (i = 0; i < n; i++) {
            float angle = fabs_(buf[i] - q->prev);
            int p = q->src + i;
            q->prev = buf[i];
            q->pole0 += (angle - q->pole0) * (1.f / 512.f);
            q->pole1 += (q->pole0 - q->pole1) * (1.f / 512.f);
            angle = q->pole1;
            if (q->warm > 0) {
                q->warm--;
            } else {
                if ((p & (DELUGE_PIX - 1)) == DELUGE_PIX / 2) {
                    float d = fabs_(angle - q->last), r = angle > 1e-12f ? tanh_(512.f * d / angle) : 0.f;
                    q->cur = (int32_t)(r * 255.f + 0.5f);
                }
                if (((p + 1) & (DELUGE_PIX - 1)) == 0 || p + 1 == length) {
                    q->ring[q->next & (DELUGE_PERC_RING - 1)] = (uint8_t)q->cur;
                    q->next++;
                    q->cur = 0;
                }
            }
            q->last = angle;
        }
        q->src += n;
        if (q->src >= length) {
            /* The file's end: the last pixels as deluge_perc leaves them, then from its start. */
            while (q->next % n_pix != 0)
                q->ring[q->next++ & (DELUGE_PERC_RING - 1)] = 0;
            q->src = 0;
            q->prev = q->pole0 = q->pole1 = q->last = 0.f;
            q->cur = 0;
        }
    }
    if (q->next - q->lo > DELUGE_PERC_RING)
        q->lo = q->next - DELUGE_PERC_RING;
}

int deluge_perc_at(const DelugeVoice *v, int p, int n_pix)
{
    const DelugePerc *q = &v->perc;
    int d, u;
    if (q->next <= 0 || n_pix <= 0)
        return -1;
    d = wrap(p - q->at, n_pix);
    if (d >= (n_pix + 1) / 2)
        d -= n_pix;
    u = q->at + d;
    while (u >= q->next && u - n_pix >= q->lo)
        u -= n_pix;
    while (u < q->lo)
        u += n_pix;
    return u < q->next ? q->ring[u & (DELUGE_PERC_RING - 1)] : -1;
}

static int perc_val(const DelugeVoice *v, int p, int n_pix)
{
    int x = deluge_perc_at(v, p, n_pix);
    return x > 0 ? x : 0;
}
/* hop() copies the pixels its beam search reads into pc[] first (the sums are of whole numbers:
 * exactly the same totals). */
#define PERC(p) ((unsigned)((p) - pc_lo) < (unsigned)pc_n ? (int)pc[(p) - pc_lo] : perc_val(v, (p), n_pix))
#else
#define PERC(p) perc[wrap((p), n_pix)]
#endif

static float lerp_table(const int16_t *t, float x, int n)
{
    int i;
    if (x < 0.f)
        x = 0.f;
    if (x > (float)(n - 1))
        x = (float)(n - 1);
    i = (int)x;
    if (i >= n - 1)
        return (float)t[n - 1];
    return (float)t[i] + (x - (float)i) * (float)(t[i + 1] - t[i]);
}

typedef struct { float min_w, max_w, xf_prop, xf_abs, random; } HopParams;

static HopParams hop_params(float ratio)
{
    HopParams h;
    float l = log2_(ratio);
    if (l >= -1.f && l < 1.f) {
        float x = (l + 1.f) * 8.f;
        h.min_w = lerp_table(min_hop_fine, x, 17);
        h.max_w = lerp_table(max_hop_fine, x, 17);
        h.xf_prop = lerp_table(xf_prop_fine, x, 17);
        h.xf_abs = lerp_table(xf_abs_fine, x, 17);
        h.random = lerp_table(random_fine, x, 17);
    } else {
        float x = l + 2.f;
        h.min_w = lerp_table(min_hop_coarse, x, 5);
        h.max_w = lerp_table(max_hop_coarse, x, 5);
        h.xf_prop = lerp_table(xf_prop_coarse, x, 5);
        h.xf_abs = lerp_table(xf_abs_coarse, x, 5);
        h.random = lerp_table(random_coarse, x, 5);
    }
    h.min_w *= SR_SCALE;
    h.max_w *= SR_SCALE;
    h.xf_abs *= SR_SCALE;
    return h;
}

static float noise(DelugeVoice *v)
{
    v->noise ^= v->noise << 13;
    v->noise ^= v->noise >> 17;
    v->noise ^= v->noise << 5;
    return (float)(int32_t)v->noise * (1.f / 2147483648.f);
}

static void head_get(DelugeHead *h, void *sample, int length, int stereo,
                     DelugeReadFn read, float *l, float *r)
{
    int k = h->pos - h->cache_pos;
    if (h->cache_n == 0 || k < 0 || k >= h->cache_n) {
        int i;
        read_wrap(read, sample, length, h->cl, h->pos, DELUGE_CACHE, 0);
        if (stereo)
            read_wrap(read, sample, length, h->cr, h->pos, DELUGE_CACHE, 1);
        else
            for (i = 0; i < DELUGE_CACHE; i++)
                h->cr[i] = h->cl[i];
        h->cache_pos = h->pos;
        h->cache_n = DELUGE_CACHE;
        k = 0;
    }
    *l = h->cl[k];
    *r = h->cr[k];
}

/* Three consecutive DELUGE_AVG sums starting at x. */
static void totals(const float *x, float *t)
{
    int i, k;
    for (k = 0; k < DELUGE_N_AVG; k++) {
        float s = 0.f;
        for (i = 0; i < DELUGE_AVG; i++)
            s += x[k * DELUGE_AVG + i];
        t[k] = s;
    }
}

#ifdef DELUGE_FAST_SEARCH
/* totals() from running sums S (S[i] = x[0] + ... + x[i]) of the new head's scratch: one subtraction
 * per average instead of DELUGE_AVG additions (the phase search's cost, ~1M instructions a hop on
 * the M7, to ~0.1M). Rounds a little differently from summing each window. */
static void totals_s(const float *S, int a, float *t)
{
    int k;
    for (k = 0; k < DELUGE_N_AVG; k++) {
        int lo = a + k * DELUGE_AVG - 1, hi = lo + DELUGE_AVG;
        t[k] = S[hi] - (lo >= 0 ? S[lo] : 0.f);
    }
}
#define TOTALS_NEW(a, t) totals_s(v->scratch, (a), (t))
#else
#define TOTALS_NEW(a, t) totals(v->scratch + (a), (t))
#endif

static float diff_abs(const float *a, const float *b)
{
    float s = 0.f;
    int k;
    for (k = 0; k < DELUGE_N_AVG; k++)
        s += fabs_(b[k] - a[k]);
    return s;
}

static float change(const float *a, const float *b)
{
    float s = 0.f;
    int k;
    for (k = 0; k < DELUGE_N_AVG; k++)
        s += b[k] - a[k];
    return s;
}

/* hopEnd(): pick the next beam, phase-align it, start the crossfade. */
static void hop(DelugeVoice *v, void *sample, int length, int stereo, DelugeReadFn read,
                const uint8_t *perc, int n_pix, float ratio)
{
    HopParams h = hop_params(ratio);
    float pos = (float)v->host_i + v->host_f, best_w, xf;
    int back, til, w;
#ifdef DELUGE_PERC_RING
    uint8_t pc[DELUGE_PERC_RING];
    int pc_lo = 0, pc_n = 0;
#endif

    if (h.random > 0.f)
        h.min_w += h.min_w * noise(v) * h.random * (1.f / 128.f);
    best_w = 0.5f * (h.min_w + h.max_w);
#ifdef DELUGE_PERC_RING
    {
        int b0 = (int)(pos + h.min_w * (ratio - 1.f) * 0.5f) >> DELUGE_PIX_SHIFT;
        int b1 = (int)(pos + h.max_w * (ratio - 1.f) * 0.5f) >> DELUGE_PIX_SHIFT;
        int f1 = (int)(pos + h.max_w * (ratio + 1.f) * 0.5f) >> DELUGE_PIX_SHIFT, j;
        pc_lo = (b0 < b1 ? b0 : b1) - 1;
        pc_n = f1 + 2 - pc_lo;
        if (pc_n > DELUGE_PERC_RING)
            pc_n = DELUGE_PERC_RING;
        if (pc_n < 0)
            pc_n = 0;
        for (j = 0; j < pc_n; j++)
            pc[j] = (uint8_t)perc_val(v, pc_lo + j, n_pix);
    }
#endif

    /* Beam width with the highest average percussiveness. */
#ifdef DELUGE_PERC_RING
    (void)perc;
    if (n_pix > 0) {
#else
    if (perc && n_pix > 0) {
#endif
        float best_total = 0.f, best_pix = 1.f;
        for (w = (int)h.min_w; (float)w < h.max_w; w += DELUGE_PIX) {
            int b = (int)((pos + (float)w * (ratio - 1.f) * 0.5f)) >> DELUGE_PIX_SHIFT;
            int f = (int)((pos + (float)w * (ratio + 1.f) * 0.5f)) >> DELUGE_PIX_SHIFT;
            float total = 0.f;
            int p;
            if (f <= b)
                continue;
            for (p = b; p < f; p++)
                total += (float)PERC(p);
            if (total * best_pix >= best_total * (float)(f - b)) {
                best_total = total;
                best_pix = (float)(f - b);
                best_w = (float)w;
            }
        }
    }

    back = (int)(pos + best_w * (ratio - 1.f) * 0.5f);
    til = (int)best_w;
    if (til < 1)
        til = 1;
    xf = (float)til * h.xf_prop * (1.f / 256.f) + h.xf_abs * 4.f;
    if (xf >= (float)(til >> 1))
        xf = (float)(til >> 1);
    til -= (int)xf;
    if (xf > (float)til)
        xf = (float)til;
    if (xf < 1.f)
        xf = 1.f;

    /* Phase search: slide the new head so three short moving averages around
     * mid-crossfade match the old head's. Stop after 4 sign flips. */
    if (v->newer.active) {
        int half = (int)xf >> 1, span = DELUGE_N_AVG * DELUGE_AVG;
        int max_search = (til * 40) >> 8, d, flips = 0, best_off = 0, dirs = 0;
        float old_t[DELUGE_N_AVG], new_t[DELUGE_N_AVG], best_diff, initial;
        if (max_search > DELUGE_SEARCH_MAX)
            max_search = DELUGE_SEARCH_MAX;
        read_mono(v, read, sample, length, stereo, SCRATCH_OLD(v),
                  v->newer.pos + half - span / 2, span);
        totals(SCRATCH_OLD(v), old_t);
        /* scratch[0] = new mid-crossfade window start - max_search */
        read_mono(v, read, sample, length, stereo, v->scratch,
                  back + half - span / 2 - max_search, span + 2 * max_search);
#ifdef DELUGE_FAST_SEARCH
        {
            float acc = 0.f;
            int j;
            for (j = 0; j < span + 2 * max_search; j++) {
                acc += v->scratch[j];
                v->scratch[j] = acc;
            }
        }
#endif
        TOTALS_NEW(max_search, new_t);
        best_diff = diff_abs(old_t, new_t);
        initial = change(old_t, new_t);
        d = 1;
        while (dirs < 2 && flips < 4) {
            float last = initial;
            int off;
            for (off = 1; off <= max_search; off++) {
                float t[DELUGE_N_AVG], dabs, c;
                TOTALS_NEW(max_search + off * d, t);
                dabs = diff_abs(old_t, t);
                if (off == 1 && d == 1 && dirs == 0 && dabs > best_diff) {
                    d = -1; /* first step worse: search the other way first */
                    off = 0;
                    continue;
                }
                if (dabs < best_diff) {
                    best_diff = dabs;
                    best_off = off * d;
                }
                c = change(old_t, t);
                if ((c < 0.f) != (last < 0.f) && ++flips >= 4)
                    break;
                last = c;
            }
            dirs++;
            d = -d;
        }
        back += best_off;
    }

    v->older = v->newer;
    v->newer.pos = wrap(back, length);
    v->newer.active = 1;
    v->newer.cache_n = 0;
    v->til_hop = til;
    v->xf = 0.f;
    v->xf_inc = 1.f / xf;
    v->n_hops++;
}

void deluge_process(DelugeVoice *v, void *sample, int length, int stereo,
                    DelugeReadFn read, const uint8_t *perc, int n_pix,
                    float *dst_l, float *dst_r, int n, float ratio, int reset)
{
    int i;
#ifdef DELUGE_RESYNC
    int force;
#endif
    if (!v || !dst_l || n <= 0 || length <= 0 || !read)
        return;
    if (v->magic != DELUGE_MAGIC || reset)
        deluge_init(v);
#ifdef DELUGE_SHARED_SCRATCH
    if (!v->scratch)
        return;
#endif
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    if (!v->started) {
        v->started = 1;
        v->newer.pos = 0;
        v->newer.active = 1;
        v->til_hop = 200 * 48 / 44; /* kDefaultFirstHopLength */
        v->xf = 1.f;
    }
#ifdef DELUGE_PERC_RING
    n_pix = (length >> DELUGE_PIX_SHIFT) + 1;
    perc_step(v, sample, length, stereo, read, 8 * n);
#endif
#ifdef DELUGE_RESYNC
    {
        /* The play head away from the play position (the clock moved it): hop now. */
        int d = wrap(v->newer.pos - v->host_i, length), lim = fabs_(ratio - 1.f) > 1e-6f ? DELUGE_RESYNC : DELUGE_RESYNC_1X;
        if (d >= (length + 1) / 2)
            d -= length;
        force = v->xf >= 1.f && (d > lim || d < -lim);
        if (force)
            v->til_hop = 1;
    }
#define FORCE_HOP || force
#else
#define FORCE_HOP
#endif
    for (i = 0; i < n; i++) {
        float nl, nr, yl, yr;
        /* No stretching at exactly 1x (the Deluge doesn't stretch there). */
        if (--v->til_hop <= 0 && (fabs_(ratio - 1.f) > 1e-6f FORCE_HOP)) {
            hop(v, sample, length, stereo, read, perc, n_pix, ratio);
#ifdef DELUGE_RESYNC
            force = 0;
#endif
        }
        head_get(&v->newer, sample, length, stereo, read, &nl, &nr);
        yl = nl;
        yr = nr;
        if (v->xf < 1.f && v->older.active) {
            float ol, orr, w = v->xf;
            head_get(&v->older, sample, length, stereo, read, &ol, &orr);
            yl = nl * w + ol * (1.f - w);
            yr = nr * w + orr * (1.f - w);
            v->older.pos++;
            v->xf += v->xf_inc;
        } else {
            v->older.active = 0;
        }
        v->newer.pos++;
        if (v->newer.pos >= length) {
            v->newer.pos -= length;
            v->newer.cache_n = 0;
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
