/* mc_proto: offline renderer and self-checks for the master chain DSP.
 *
 *   mc_proto [check [outdir]]                 self-checks on the built-in demo loop
 *   mc_proto render in.wav out.wav [id=v ...] whole chain (defaults + overrides, bpm=<n> sets tempo)
 *   mc_proto stage clr|cmp in.wav out.wav [id=v ...]   one ported stage alone (vs ref_render)
 *   mc_proto diff a.wav b.wav                 error of b against a, in dB below a
 *
 * Exit status is non-zero if a check fails.
 */
#include "mc_dsp.h"
#include "wavio.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000

static int apply_args(McParams *p, float *bpm, int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char key[64];
        const char *eq = strchr(argv[i], '=');
        if (!eq || eq - argv[i] >= (long)sizeof key) { fprintf(stderr, "bad argument: %s\n", argv[i]); return -1; }
        memcpy(key, argv[i], (size_t)(eq - argv[i]));
        key[eq - argv[i]] = 0;
        float v = (float)atof(eq + 1);
        if (!strcmp(key, "bpm")) { *bpm = v; continue; }
        if (!strcmp(key, "preset") && !strcmp(eq + 1, "stock")) { mc_stock_params(p); continue; }
        int k = mc_param_find(key);
        if (k < 0) { fprintf(stderr, "unknown parameter: %s\n", key); return -1; }
        mc_param_set(p, k, v);
    }
    return 0;
}

typedef struct { float peak_db, cmp_gr, max_gr, max_over, lim_gr, gr_sum; long blocks; float det[MC_BANDS], det_sum[MC_BANDS], eq[MC_BANDS]; } Stats;

static void run_chain(const McParams *p, float bpm, float *L, float *R, long n, int sr, Stats *s)
{
    McChain c;
    mc_init(&c, (float)sr);
    mc_set_params(&c, p);
    mc_set_bpm(&c, bpm);
    memset(s, 0, sizeof *s);
    s->peak_db = -120.f;
    for (long pos = 0; pos < n; pos += 512) {
        int k = (int)(n - pos < 512 ? n - pos : 512);
        mc_process(&c, L + pos, R + pos, k);
        const McMeters *m = &c.m;
        if (m->out_peak_db > s->peak_db) s->peak_db = m->out_peak_db;
        if (m->cmp_gr_db < s->cmp_gr) s->cmp_gr = m->cmp_gr_db;
        if (m->max_gr_db < s->max_gr) s->max_gr = m->max_gr_db;
        if (m->max_over_db > s->max_over) s->max_over = m->max_over_db;
        if (m->lim_gr_db < s->lim_gr) s->lim_gr = m->lim_gr_db;
        s->gr_sum += m->max_gr_db;
        s->blocks++;
        for (int b = 0; b < MC_BANDS; b++) {
            if (s->blocks == 1 || m->eq_det_db[b] > s->det[b]) s->det[b] = m->eq_det_db[b];
            s->det_sum[b] += m->eq_det_db[b];
            if (fabsf(m->eq_dyn_db[b]) > fabsf(s->eq[b])) s->eq[b] = m->eq_dyn_db[b];
        }
    }
}

static double diff_db(const float *aL, const float *aR, const float *bL, const float *bR, long n)
{
    double err = 0, ref = 0;
    for (long i = 0; i < n; i++) {
        double dl = aL[i] - bL[i], dr = aR[i] - bR[i];
        err += dl * dl + dr * dr;
        ref += (double)aL[i] * aL[i] + (double)aR[i] * aR[i];
    }
    return 10.0 * log10((err + 1e-30) / (ref + 1e-30));
}

static int cmd_check(const char *outdir)
{
    long n = SR * 8;
    int fails = 0;
    float *inL = malloc(sizeof(float) * n), *inR = malloc(sizeof(float) * n);
    float *L = malloc(sizeof(float) * n), *R = malloc(sizeof(float) * n);
    McDemo d;
    mc_demo_init(&d, SR);
    mc_demo_fill(&d, inL, inR, n);
    Stats s;
    McParams p;
    char path[512];

    /* 1. chain off: no master processing, only the stock limiter as a safety. A quiet input passes
     *    untouched; a hot one stays at or under 0 dBFS. */
    mc_stock_params(&p);
    for (long i = 0; i < n; i++) { L[i] = inL[i] * 0.5f; R[i] = inR[i] * 0.5f; }
    run_chain(&p, 120.f, L, R, n, SR, &s);
    long diff = 0;
    for (long i = 0; i < n; i++)
        if (L[i] != inL[i] * 0.5f || R[i] != inR[i] * 0.5f) diff++;
    printf("chain off, quiet input: %ld samples changed (need 0)\n", diff);
    if (diff) { printf("  FAIL\n"); fails++; }
    for (long i = 0; i < n; i++) { L[i] = inL[i] * 2.0f; R[i] = inR[i] * 2.0f; }
    run_chain(&p, 120.f, L, R, n, SR, &s);
    printf("chain off, hot input: peak %.2f dB, limiter GR %.2f dB (need peak <= 0)\n", s.peak_db, s.lim_gr);
    if (s.peak_db > 0.001f || s.lim_gr > -0.1f) { printf("  FAIL\n"); fails++; }
    snprintf(path, sizeof path, "%s/chain_off.wav", outdir);
    wav_write_f32(path, L, R, n, SR);

    /* 2. default chain: never above the -0.3 dBFS ceiling, safety clamp never needed */
    mc_default_params(&p);
    printf("\n%-8s %8s %8s %8s %8s %10s\n", "bpm", "peak dB", "cmp GR", "max GR", "over", "mean MAX GR");
    float means[3];
    const float bpms[3] = { 90.f, 120.f, 174.f };
    for (int k = 0; k < 3; k++) {
        memcpy(L, inL, sizeof(float) * n); memcpy(R, inR, sizeof(float) * n);
        run_chain(&p, bpms[k], L, R, n, SR, &s);
        means[k] = s.gr_sum / (float)s.blocks;
        printf("%-8.0f %8.2f %8.2f %8.2f %8.2f %10.2f\n", bpms[k], s.peak_db, s.cmp_gr, s.max_gr, s.max_over, means[k]);
        if (s.peak_db > 20.f * log10f(MC_MAX_CEILING) + 0.01f) { printf("  FAIL: peak above ceiling\n"); fails++; }
        /* float rounding of ceil/peak * peak can land one ulp over; anything real is far above this */
        if (s.max_over > 0.01f) { printf("  FAIL: safety clamp engaged (look-ahead missed a peak)\n"); fails++; }
        if (k == 1) {
            snprintf(path, sizeof path, "%s/default.wav", outdir);
            wav_write_f32(path, L, R, n, SR);
        }
    }
    /* 3. release follows tempo: faster tempo = shorter release = less average gain reduction */
    printf("release follows tempo: %s\n", (means[0] < means[1] && means[1] < means[2]) ? "yes" : "NO");
    if (!(means[0] < means[1] && means[1] < means[2])) fails++;

    printf(fails ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", fails);
    free(inL); free(inR); free(L); free(R);
    return fails ? 1 : 0;
}

static int cmd_render(int argc, char **argv, int stage)
{
    float *L, *R, bpm = 120.f;
    int sr;
    const char *which = stage ? argv[0] : 0;
    if (stage) { argc--; argv++; }
    if (argc < 2) { fprintf(stderr, "need in.wav out.wav\n"); return 2; }
    long n = wav_read(argv[0], &L, &R, &sr);
    if (n < 0) return 1;
    McParams p;
    mc_default_params(&p);
    if (stage) {   /* stage tests start from the plugins' own defaults */
        p.clr_sidepass = p.clr_glue = 0.f;
        p.clr_scope = p.clr_skronk = p.clr_girth = p.clr_drive = 0.5f;
        p.sq_squish = p.sq_bassbloom = 0.f; p.sq_output = p.sq_mix = 1.f;
    }
    if (apply_args(&p, &bpm, argc - 2, argv + 2)) return 2;
    if (stage) {
        if (!strcmp(which, "clr")) {
            McClr s; mc_clr_reset(&s);
            for (long pos = 0; pos < n; pos += 512) {
                int k = (int)(n - pos < 512 ? n - pos : 512);
                mc_clr_process(&s, (float)sr, &p, L + pos, R + pos, k, 0);
            }
        } else if (!strcmp(which, "cmp")) {
            McCmp s; mc_cmp_reset(&s);
            for (long pos = 0; pos < n; pos += 512) {
                int k = (int)(n - pos < 512 ? n - pos : 512);
                mc_cmp_process(&s, (float)sr, &p, L + pos, R + pos, k, 0);
            }
        } else { fprintf(stderr, "stage must be clr or cmp\n"); return 2; }
    } else {
        Stats s;
        run_chain(&p, bpm, L, R, n, sr, &s);
        printf("peak %.2f dB  cmp GR %.2f  max GR %.2f (mean %.2f)  over %.2f\n", s.peak_db, s.cmp_gr,
               s.max_gr, s.gr_sum / (float)(s.blocks ? s.blocks : 1), s.max_over);
        if (p.eq_on > 0.5f)
            for (int b = 0; b < MC_BANDS; b++)
                printf("  B%d detector peak %6.1f dB  mean %6.1f dB  dyn max %5.1f dB\n", b + 1, s.det[b],
                       s.det_sum[b] / (float)s.blocks, s.eq[b]);
    }
    int rc = wav_write_f32(argv[1], L, R, n, sr);
    free(L); free(R);
    return rc ? 1 : 0;
}

static int cmd_diff(const char *a, const char *b)
{
    float *aL, *aR, *bL, *bR;
    int sa, sb;
    long na = wav_read(a, &aL, &aR, &sa), nb = wav_read(b, &bL, &bR, &sb);
    if (na < 0 || nb < 0) return 1;
    long n = na < nb ? na : nb;
    double peak = 0;
    for (long i = 0; i < n; i++) {
        double e = fmax(fabs(aL[i] - bL[i]), fabs(aR[i] - bR[i]));
        if (e > peak) peak = e;
    }
    printf("error %.1f dB below signal, peak error %.1f dBFS (%ld frames)\n",
           -diff_db(aL, aR, bL, bR, n), 20.0 * log10(peak + 1e-30), n);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "check")) return cmd_check(argc > 2 ? argv[2] : "out");
    if (!strcmp(argv[1], "render")) return cmd_render(argc - 2, argv + 2, 0);
    if (!strcmp(argv[1], "stage") && argc > 2) return cmd_render(argc - 2, argv + 2, 1);
    if (!strcmp(argv[1], "diff") && argc > 3) return cmd_diff(argv[2], argv[3]);
    fprintf(stderr, "usage: %s [check [outdir]] | render in out [id=v..] | stage clr|cmp in out [id=v..] | diff a b\n", argv[0]);
    return 2;
}
