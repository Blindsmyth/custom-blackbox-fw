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
    for (i = 0; i < sizeof(DelugeVoice); i++)
        w[i] = 0;
    v->magic = DELUGE_MAGIC;
    v->noise = 0x9E3779B9u;
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

    if (h.random > 0.f)
        h.min_w += h.min_w * noise(v) * h.random * (1.f / 128.f);
    best_w = 0.5f * (h.min_w + h.max_w);

    /* Beam width with the highest average percussiveness. */
    if (perc && n_pix > 0) {
        float best_total = 0.f, best_pix = 1.f;
        for (w = (int)h.min_w; (float)w < h.max_w; w += DELUGE_PIX) {
            int b = (int)((pos + (float)w * (ratio - 1.f) * 0.5f)) >> DELUGE_PIX_SHIFT;
            int f = (int)((pos + (float)w * (ratio + 1.f) * 0.5f)) >> DELUGE_PIX_SHIFT;
            float total = 0.f;
            int p;
            if (f <= b)
                continue;
            for (p = b; p < f; p++)
                total += (float)perc[wrap(p, n_pix)];
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
        read_mono(v, read, sample, length, stereo, v->scratch_old,
                  v->newer.pos + half - span / 2, span);
        totals(v->scratch_old, old_t);
        /* scratch[0] = new mid-crossfade window start - max_search */
        read_mono(v, read, sample, length, stereo, v->scratch,
                  back + half - span / 2 - max_search, span + 2 * max_search);
        totals(v->scratch + max_search, new_t);
        best_diff = diff_abs(old_t, new_t);
        initial = change(old_t, new_t);
        d = 1;
        while (dirs < 2 && flips < 4) {
            float last = initial;
            int off;
            for (off = 1; off <= max_search; off++) {
                float t[DELUGE_N_AVG], dabs, c;
                totals(v->scratch + max_search + off * d, t);
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
    if (!v || !dst_l || n <= 0 || length <= 0 || !read)
        return;
    if (v->magic != DELUGE_MAGIC || reset)
        deluge_init(v);
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
    for (i = 0; i < n; i++) {
        float nl, nr, yl, yr;
        /* No stretching at exactly 1x (the Deluge doesn't stretch there). */
        if (--v->til_hop <= 0 && fabs_(ratio - 1.f) > 1e-6f)
            hop(v, sample, length, stereo, read, perc, n_pix, ratio);
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
