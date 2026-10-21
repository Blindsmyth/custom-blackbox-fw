/* Blackbox 3.1.9 Granular pad engine, modelled from the decompile (docs/map-319.md "Granular pad
 * engine"). Freestanding: no libm (the lab builds it to wasm with -nostdlib). */
#include "granular.h"

#define PI_F 3.14159265f

static float hann[8192];          /* stock table 0x080D26AC: 0.5 - 0.5 cos(2 pi i / 8192) */
static float pan_l[256], pan_r[256];
static int tables;

/* Scatter: stock table 0x080EB2A0, 97 entries, exponential, 0..39317 samples. Approximated by an
 * exponential through the reported points (0, 1, 2 ... 32493, 35743, 39317). */
static int scatter_tab[97];

static float sin_r(float x)       /* |x| <= pi */
{
    float x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6 + x2 * (1.f / 120 + x2 * (-1.f / 5040 + x2 * (1.f / 362880 + x2 * (-1.f / 39916800))))));
}

static float cos_r(float x)       /* |x| <= pi */
{
    float x2 = x * x;
    return 1.f + x2 * (-0.5f + x2 * (1.f / 24 + x2 * (-1.f / 720 + x2 * (1.f / 40320 + x2 * (-1.f / 3628800 + x2 * (1.f / 479001600))))));
}

static double exp2_d(double x)    /* 2^x */
{
    int i = (int)x;
    if (x < i)
        i--;
    double f = x - i, r = 1., t = 1., l = 0.69314718055994530942 * f;
    for (int k = 1; k < 16; k++) {
        t *= l / k;
        r += t;
    }
    while (i > 0) {
        r *= 2.;
        i--;
    }
    while (i < 0) {
        r *= 0.5;
        i++;
    }
    return r;
}

static void make_tables(void)
{
    if (tables)
        return;
    for (int i = 0; i < 8192; i++) {
        float a = 2.f * PI_F * i / 8192.f;
        if (a > PI_F)
            a -= 2.f * PI_F;
        hann[i] = 0.5f - 0.5f * cos_r(a);
    }
    for (int i = 0; i < 256; i++) {   /* pan index (pan + 1) * 128: L = cos, R = sin of pi/4 (pan + 1) */
        float a = PI_F / 4.f * (i / 128.f);
        pan_l[i] = cos_r(a);
        pan_r[i] = sin_r(a);
    }
    for (int i = 0; i < 97; i++)
        scatter_tab[i] = i == 0 ? 0 : (int)(exp2_d((i - 1) * 15.2627 / 95.0) + 0.5);
    tables = 1;
}

/* ANSI LCG, as stock FUN_080c89d4 (seed 1 at boot). */
static int rnd(GranVoice *v)
{
    v->seed = v->seed * 0x41C64E6Du + 12345u;
    return (int)((v->seed & 0x7FFFFFFFu) >> 16);
}

static int64_t round64(double x) { return (int64_t)(x >= 0 ? x + 0.5 : x - 0.5); }

void gran_init(GranVoice *v)
{
    make_tables();
    GranVoice z = { 0 };
    *v = z;
    v->size_v = 82;       /* about 4096 samples, the stock default */
    v->density_v = 250;
    v->speed_v = 1000;
    v->seed = 1;
    gran_set(v, v->size_v, v->density_v, 0, 0, 1000, 0, 0, 0);
}

void gran_set(GranVoice *v, int size, int density, int scatter, int window, int speed, int panrnd,
              int root, int sync)
{
    make_tables();
    v->size_v = size;
    v->density_v = density;
    v->scatter_v = scatter;
    v->window_v = window;
    v->speed_v = speed;
    v->panrnd_v = panrnd;
    v->root = root;
    v->sync = sync;
    /* Grain Size (0x17C): max(840, floor((1200 + v * 47.744) / 840) * 840) */
    int x = (int)(1200 + size * 0.001 * 47744);
    v->size = x / 840 * 840;
    if (v->size < 840)
        v->size = 840;
    /* Density (0x180) */
    float d = density * 0.001f;
    if (d < 0.5f) {
        float q = (0.5f - d) * 2.f;
        v->n = 3;
        v->gap = (int)(256 + q * q * q * 9.f * 48000.f) / 3;
    } else {
        v->n = (int)((d - 0.5f) * 2.f * 5.f + 3.f);
        if (v->n > GRAN_SLOTS)
            v->n = GRAN_SLOTS;
        v->gap = 0;
    }
    /* Scatter (0x17E): a constant time offset (only when gap != 0) */
    int si = scatter * 96 / 1000;
    v->scatter = scatter_tab[si < 0 ? 0 : si > 96 ? 96 : si];
    /* Window (0x5B): the random position range, (v/1000)^3 * 38 s */
    float w = window * 0.001f;
    int wi = (int)(256 + w * w * w * 38.f * 48000.f) - 256;
    v->window = wi < 0 ? 0 : wi;
    v->speed = speed * 0.001f;
    v->panrnd = panrnd * 0.001f;
}

static float scan_rate(GranVoice *v, float ratio) { return v->sync ? ratio : v->speed; }

/* FUN_08057ab0: (re)start grain k. note_on: the base clock restarts at now. */
static void grain_start(GranVoice *v, int k, int note_on, int length, float ratio)
{
    GranGrain *g = &v->g[k];
    int size = v->size, n = v->n;
    int64_t s = (int64_t)n * v->gap + size, t;
    float x = scan_rate(v, ratio);
    if (k == 0) {
        v->base = note_on ? v->now : v->base + s;
        t = v->base;
    } else {
        t = v->base + (int64_t)k * s / n + (v->gap ? v->scatter : 0);
    }
    double centre = v->play + (double)round64((double)(t - v->now) * x);
    if (v->window)
        centre += (double)((int64_t)((uint32_t)rnd(v) * (uint32_t)rnd(v)) % v->window) - v->window / 2;
    if (v->root) {
        /* Period lock: grains keep the phase of the root note's period against the clock. */
        int p = (int)(48000. / (32.703 * exp2_d((v->root - 24) / 12.)));
        if (p > 1) {
            int64_t a = ((int64_t)centre - v->ref_play) % p, b = (t - v->ref_time) % p;
            if (a < 0)
                a += p;
            if (b < 0)
                b += p;
            int64_t dlt = a - b;
            if (dlt > p / 2)
                dlt -= p;
            if (dlt <= -p / 2)
                dlt += p;
            centre -= (double)dlt;
        }
    }
    float pan = 0.f;
    if (v->panrnd != 0.f)
        pan = (float)((rnd(v) % 256) - 128) * v->panrnd / 128.f;
    int pi = (int)((pan + 1.f) * 128.f);
    pi = pi < 0 ? 0 : pi > 255 ? 255 : pi;
    g->gl = pan_l[pi];
    g->gr = pan_r[pi];
    int64_t lead = t - size / 2 - v->now;
    int64_t start = (int64_t)centre - round64(x * size / 2.f);
    if (lead > 0) {
        g->delay = (int)lead;
        g->win = 0;
        g->remain = size;
    } else {
        int64_t skip = v->now - (t - size / 2);
        if (skip > size - 1)
            skip = size - 1;
        g->delay = 0;
        g->win = (int)skip;
        g->remain = size - (int)skip;
        start += skip;
    }
    if (start < 0)
        start = 0;
    (void)length;
    g->pos = start;
    g->size = size;
    g->active = 1;
}

static float src[2][4096];

void gran_process(GranVoice *v, void *sample, int length, int stereo, GranReadFn read, float *out_l,
                  float *out_r, int n, float ratio, int reset)
{
    if (length <= 0 || n <= 0)
        return;
    if (n > 4096)
        n = 4096;
    if (reset) {
        v->now = 0;
        v->play = 0.;
        v->seed = 1;
        v->ref_play = 0;
        v->ref_time = 0;
        for (int k = 0; k < GRAN_SLOTS; k++)
            v->g[k].active = 0;
        for (int k = 0; k < v->n; k++)
            grain_start(v, k, 1, length, ratio);
    }
    for (int k = 0; k < GRAN_SLOTS; k++) {
        GranGrain *g = &v->g[k];
        int at = 0, restarts = 0;
        while (g->active && at < n && restarts < 2) {
            if (g->delay >= n - at) {
                g->delay -= n - at;
                break;
            }
            at += g->delay;
            g->delay = 0;
            int m = n - at < g->remain ? n - at : g->remain;
            /* Read m frames from the loop (the lab's clips loop), forward. */
            for (int c = 0; c < 2; c++) {
                int got = 0;
                while (got < m) {
                    int p = (int)((g->pos + got) % length);
                    int k2 = length - p < m - got ? length - p : m - got;
                    read(sample, src[c] + got, k2, p, stereo ? c : 0);
                    got += k2;
                }
            }
            for (int i = 0; i < m; i++) {
                float w = hann[((int64_t)(g->win + i) << 13) / g->size];
                out_l[at + i] += src[0][i] * w * g->gl;
                out_r[at + i] += src[1][i] * w * g->gr;
            }
            g->pos += m;
            g->win += m;
            g->remain -= m;
            at += m;
            if (g->remain <= 0) {
                v->now += at;            /* the grain restarts at this point of the block */
                grain_start(v, k, 0, length, ratio);
                v->now -= at;
                restarts++;
            }
        }
        if (k >= v->n)
            g->active = 0;
    }
    /* The playhead scans at Speed (stock), or at the tempo ratio locked to the clock (sync). */
    v->now += n;
    v->play += (double)n * scan_rate(v, ratio);   /* sync: frames x tempo ratio, so a tempo change
                                                     bends the scan, it does not jump it */
    if (v->play >= length * 4.0) {
        v->play -= length;
        v->ref_play -= length;
    }
}
