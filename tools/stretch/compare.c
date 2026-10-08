/* Host A/B: stock-like 256-sample grains vs Smooth.
 * Build: clang -O2 -o /tmp/smooth_compare tools/stretch/compare.c tools/stretch/smooth.c -lm
 */
#include "smooth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000
#define NSEC 2
#define NSAMP (SR * NSEC)

static int read_linear(void *sample, float *dest, int count, int pos, int chan)
{
    const float *src = ((const float **)sample)[chan ? 1 : 0];
    int i;
    for (i = 0; i < count; i++)
        dest[i] = src[pos + i];
    (void)chan;
    return 1;
}

static void naive_grain(const float *src, int length, float *dst, int n, float ratio)
{
    int win = 256, hop = 128, pos = 0, out = 0;
    float acc[256];
    int emitted = hop;
    int have = 0;
    memset(acc, 0, sizeof acc);
    while (out < n) {
        int i, take;
        if (!have || emitted >= hop) {
            int p, ip;
            if (have) {
                for (i = 0; i < hop; i++)
                    acc[i] = acc[i + hop];
                for (i = hop; i < win; i++)
                    acc[i] = 0.f;
            }
            ip = pos;
            if (ip < 0)
                ip = 0;
            for (i = 0; i < win; i++) {
                float t = (float)i / (float)win;
                float w = 0.5f * (1.f - cosf(6.28318530718f * t));
                p = ip + i;
                while (p >= length)
                    p -= length;
                acc[i] += src[p] * w;
            }
            pos += (int)((float)hop * ratio);
            while (length > 0 && pos >= length)
                pos -= length;
            have = 1;
            emitted = 0;
        }
        take = hop - emitted;
        if (take > n - out)
            take = n - out;
        for (i = 0; i < take; i++)
            dst[out + i] += acc[emitted + i];
        emitted += take;
        out += take;
    }
}

static void render_smooth(SmoothVoice *v, const float *src, int length,
                          float *dst, int n, float ratio, int reset)
{
    const float *chs[2];
    chs[0] = src;
    chs[1] = src;
    smooth_process(v, chs, length, 0, read_linear, dst, NULL, n, ratio, reset);
}

static float corr(const float *a, const float *b, int n)
{
    double aa = 0, bb = 0, ab = 0;
    int i;
    for (i = 0; i < n; i++) {
        aa += (double)a[i] * a[i];
        bb += (double)b[i] * b[i];
        ab += (double)a[i] * b[i];
    }
    if (aa < 1e-12 || bb < 1e-12)
        return 0.f;
    return (float)(ab / sqrt(aa * bb));
}

static float goertzel_power(const float *x, int n, float freq)
{
    float w = 6.28318530718f * freq / (float)SR;
    float coeff = 2.f * cosf(w);
    float s1 = 0.f, s2 = 0.f;
    int i;
    for (i = 0; i < n; i++) {
        float s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

static void envelope(const float *x, int n, float *env)
{
    int i;
    env[0] = fabsf(x[0]);
    for (i = 1; i < n; i++) {
        float d = x[i] - x[i - 1];
        float a = fabsf(d);
        env[i] = env[i - 1] + 0.2f * (a - env[i - 1]);
    }
}

static void make_loop(float *dst)
{
    int i;
    memset(dst, 0, NSAMP * sizeof(float));
    for (i = 0; i < NSAMP; i++) {
        float t = (float)i / (float)SR;
        dst[i] += 0.25f * sinf(2.f * 3.14159265f * 110.f * t);
    }
    for (i = 0; i < NSEC * 2; i++) {
        int at = i * (SR / 2);
        int k;
        for (k = 0; k < 600 && at + k < NSAMP; k++) {
            float e = expf(-k * 0.012f);
            dst[at + k] += 0.9f * e * sinf(2.f * 3.14159265f * 55.f * (float)k / (float)SR);
        }
    }
    for (i = 0; i < NSEC * 4; i++) {
        int at = i * (SR / 4) + SR / 8;
        int k;
        unsigned s = (unsigned)(i * 17 + 3);
        for (k = 0; k < 180 && at + k < NSAMP; k++) {
            s = s * 1664525u + 1013904223u;
            dst[at + k] += 0.15f * ((int)(s >> 16) / 32768.f - 1.f) * (1.f - (float)k / 180.f);
        }
    }
}

static int write_wav(const char *path, const float *x, int n)
{
    FILE *f = fopen(path, "wb");
    int i, data = n * 2;
    unsigned hdr[11];
    short *pcm;
    if (!f)
        return 0;
    pcm = malloc((size_t)n * 2);
    if (!pcm) {
        fclose(f);
        return 0;
    }
    for (i = 0; i < n; i++) {
        float s = x[i];
        if (s > 1.f)
            s = 1.f;
        if (s < -1.f)
            s = -1.f;
        pcm[i] = (short)(s * 32767.f);
    }
    memcpy(hdr, "RIFF", 4);
    hdr[1] = 36 + data;
    memcpy(hdr + 2, "WAVEfmt ", 8);
    hdr[4] = 16;
    hdr[5] = 1 | (1 << 16);          /* pcm, mono */
    hdr[6] = SR;
    hdr[7] = SR * 2;
    hdr[8] = 2 | (16 << 16);
    memcpy((char *)hdr + 36, "data", 4);
    /* layout above is sloppy; write the header explicitly */
    fclose(f);
    f = fopen(path, "wb");
    if (!f) {
        free(pcm);
        return 0;
    }
    {
        unsigned char h[44];
        unsigned sz = 36 + data;
        memset(h, 0, 44);
        memcpy(h, "RIFF", 4);
        memcpy(h + 4, &sz, 4);
        memcpy(h + 8, "WAVE", 4);
        memcpy(h + 12, "fmt ", 4);
        h[16] = 16;
        h[20] = 1;
        h[22] = 1;
        memcpy(h + 24, (unsigned[]){SR}, 4);
        {
            unsigned br = SR * 2;
            memcpy(h + 28, &br, 4);
        }
        h[32] = 2;
        h[34] = 16;
        memcpy(h + 36, "data", 4);
        memcpy(h + 40, &data, 4);
        fwrite(h, 1, 44, f);
    }
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);
    free(pcm);
    (void)hdr;
    return 1;
}

static float *alloc_loop(void)
{
    float *s = calloc(NSAMP, sizeof(float));
    if (!s) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    make_loop(s);
    return s;
}

int main(void)
{
    float *src = alloc_loop();
    float *smooth = calloc(NSAMP, sizeof(float));
    float *naive = calloc(NSAMP, sizeof(float));
    float *env_s = calloc(NSAMP, sizeof(float));
    float *env_n = calloc(NSAMP, sizeof(float));
    float *dry_time = calloc(NSAMP, sizeof(float));
    SmoothVoice *voice;
    float ratios[] = {0.75f, 1.f, 1.25f, 1.5f};
    int ri, fail = 0;
    float id_corr, mod_s, mod_n;
    const char *out_dir = "tools/stretch/out";
    char path[160];

    voice = calloc(1, sizeof *voice);
    if (!smooth || !naive || !env_s || !env_n || !dry_time || !voice) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    printf("Smooth window %d hop %d search ±%d (48 kHz)\n",
           SMOOTH_WIN, SMOOTH_HOP, SMOOTH_SEARCH);
    printf("Naive grains window 256 hop 128, fixed phase\n");

    for (ri = 0; ri < 4; ri++) {
        float ratio = ratios[ri];
        float c_s, c_n, rms_s = 0.f;
        int i, out_n = (int)((float)NSAMP / ratio);
        if (out_n > NSAMP)
            out_n = NSAMP;
        memset(smooth, 0, NSAMP * sizeof(float));
        memset(naive, 0, NSAMP * sizeof(float));
        smooth_init(voice);
        render_smooth(voice, src, NSAMP, smooth, out_n, ratio, 1);
        naive_grain(src, NSAMP, naive, out_n, ratio);
        for (i = 0; i < out_n; i++)
            rms_s += smooth[i] * smooth[i];
        rms_s = sqrtf(rms_s / (float)out_n);
        if (ratio == 1.f) {
            id_corr = corr(src + SMOOTH_WIN, smooth + SMOOTH_WIN, NSAMP - SMOOTH_WIN - 1000);
            printf("ratio 1.00  identity corr %0.3f  rms %0.3f\n", id_corr, rms_s);
            if (id_corr < 0.97f)
                fail = 1;
        } else {
            float p_keep = goertzel_power(smooth + 2048, out_n - 4096, 110.f);
            float p_shift = goertzel_power(smooth + 2048, out_n - 4096, 110.f * ratio);
            float n_keep = goertzel_power(naive + 2048, out_n - 4096, 110.f);
            float n_shift = goertzel_power(naive + 2048, out_n - 4096, 110.f * ratio);
            int kicks = 0, k;
            printf("ratio %0.2f  pitch110/shifted smooth %0.2f  naive %0.2f  rms %0.3f\n",
                   ratio, p_keep / (p_shift + 1.f), n_keep / (n_shift + 1.f), rms_s);
            /* Kicks sit every 0.5 s in the source. Output spacing is that / ratio. */
            for (k = 1; k < NSEC * 2; k++) {
                int expect = (int)((float)(k * (SR / 2)) / ratio);
                int a = expect - 200, b = expect + 400, j, peak = a;
                float best = 0.f;
                if (a < 0 || b >= out_n)
                    continue;
                for (j = a; j < b; j++) {
                    float m = smooth[j] < 0.f ? -smooth[j] : smooth[j];
                    if (m > best) {
                        best = m;
                        peak = j;
                    }
                }
                if (best > 0.2f && abs(peak - expect) < 400)
                    kicks++;
            }
            printf("  kicks on the stretched grid: %d\n", kicks);
            if (!(p_keep > p_shift * 2.f))
                fail = 1;
            if (kicks < 2)
                fail = 1;
            if (!(rms_s > 0.05f && rms_s < 1.5f))
                fail = 1;
            (void)c_s;
            (void)c_n;
        }
        snprintf(path, sizeof path, "%s/smooth_%0.2f.wav", out_dir, ratio);
        write_wav(path, smooth, out_n);
        snprintf(path, sizeof path, "%s/naive_%0.2f.wav", out_dir, ratio);
        write_wav(path, naive, out_n);
    }

    /* Grain-rate buzz: power of the amplitude envelope at the naive hop rate. */
    memset(smooth, 0, NSAMP * sizeof(float));
    memset(naive, 0, NSAMP * sizeof(float));
    smooth_init(voice);
    render_smooth(voice, src, NSAMP, smooth, NSAMP, 1.25f, 1);
    naive_grain(src, NSAMP, naive, NSAMP, 1.25f);
    envelope(smooth, NSAMP, env_s);
    envelope(naive, NSAMP, env_n);
    mod_s = goertzel_power(env_s, NSAMP, 375.f);
    mod_n = goertzel_power(env_n, NSAMP, 375.f);
    printf("envelope power at 375 Hz (naive grain rate): smooth %0.3f  naive %0.3f\n",
           mod_s, mod_n);
    if (!(mod_s < mod_n))
        fail = 1;

    /* Retrigger: a reset mid-loop must bring the attack back. */
    {
        float head[800], again[800];
        int i;
        memset(smooth, 0, NSAMP * sizeof(float));
        smooth_init(voice);
        render_smooth(voice, src, NSAMP, smooth, 4000, 1.f, 1);
        memset(again, 0, sizeof again);
        render_smooth(voice, src, NSAMP, again, 800, 1.f, 1);
        for (i = 0; i < 800; i++)
            head[i] = 0.f;
        render_smooth(voice, src, NSAMP, head, 800, 1.f, 0);
        /* `again` was a fresh voice reset. Compare it with a continuation. */
        smooth_init(voice);
        memset(head, 0, sizeof head);
        render_smooth(voice, src, NSAMP, head, 800, 1.f, 1);
        id_corr = corr(head + 32, again + 32, 700);
        printf("retrigger corr vs fresh attack %0.3f\n", id_corr);
        if (id_corr < 0.99f)
            fail = 1;
        (void)i;
    }

    printf("%s\n", fail ? "FAIL" : "ok");
    free(src);
    free(smooth);
    free(naive);
    free(env_s);
    free(env_n);
    free(dry_time);
    free(voice);
    return fail ? 1 : 0;
}
