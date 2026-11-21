#include "mc_dsp.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define MC_PI 3.14159265358979f

static float db2lin(float d) { return powf(10.f, d * 0.05f); }
static float lin2db(float x) { return 20.f * log10f(x + 1e-9f); }
static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

/* ---- biquad (RBJ), direct form II transposed ------------------------------------------------ */

void mc_bq_design(int type, float sr, float f, float q, float gain_db, McCoef *out)
{
    f = clampf(f, 10.f, sr * 0.45f);
    q = clampf(q, 0.1f, 20.f);
    float w = 2.f * MC_PI * f / sr, cw = cosf(w), sw = sinf(w), al = sw / (2.f * q);
    float A = powf(10.f, gain_db / 40.f), s = 2.f * sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    switch (type) {
    case MC_BQ_PEAK:
        b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
        break;
    case MC_BQ_LSHELF:
        b0 = A * ((A + 1) - (A - 1) * cw + s);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - s;
        break;
    case MC_BQ_HSHELF:
        b0 = A * ((A + 1) + (A - 1) * cw + s);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - s;
        break;
    case MC_BQ_HIGHPASS:
        b0 = (1 + cw) * 0.5f; b1 = -(1 + cw); b2 = (1 + cw) * 0.5f;
        a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
        break;
    case MC_BQ_LOWPASS:
        b0 = (1 - cw) * 0.5f; b1 = 1 - cw; b2 = (1 - cw) * 0.5f;
        a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
        break;
    default: /* band-pass, 0 dB peak */
        b0 = al; b1 = 0; b2 = -al;
        a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
        break;
    }
    out->b0 = b0 / a0; out->b1 = b1 / a0; out->b2 = b2 / a0;
    out->a1 = a1 / a0; out->a2 = a2 / a0;
}

float mc_bq_mag_db(const McCoef *c, float f, float sr)
{
    float w = 2.f * MC_PI * f / sr;
    float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2 * w), s2 = sinf(2 * w);
    float nr = c->b0 + c->b1 * c1 + c->b2 * c2, ni = -(c->b1 * s1 + c->b2 * s2);
    float dr = 1 + c->a1 * c1 + c->a2 * c2, di = -(c->a1 * s1 + c->a2 * s2);
    return 10.f * log10f((nr * nr + ni * ni) / (dr * dr + di * di + 1e-20f) + 1e-20f);
}

static float bq(const McCoef *k, float *z, float x)
{
    float y = k->b0 * x + z[0];
    z[0] = k->b1 * x - k->a1 * y + z[1];
    z[1] = k->b2 * x - k->a2 * y;
    return y;
}

/* ---- EQ, stock compressor, stock limiter ------------------------------------------------------ */

static float block_coef(const McChain *c, int n, float ms)
{
    return 1.f - expf(-(float)n / (c->sr * 0.001f * (ms < 0.01f ? 0.01f : ms)));
}

int mc_band_filter(int t)
{
    switch (t) {
    case MC_BAND_LOWCUT: return MC_BQ_HIGHPASS;
    case MC_BAND_LOWSHELF: return MC_BQ_LSHELF;
    case MC_BAND_HIGHSHELF: return MC_BQ_HSHELF;
    case MC_BAND_HIGHCUT: return MC_BQ_LOWPASS;
    default: return MC_BQ_PEAK;
    }
}

int mc_band_is_cut(int t) { return t == MC_BAND_LOWCUT || t == MC_BAND_HIGHCUT; }

static void eq_block(McChain *c, float *L, float *R, int n, McMeters *m)
{
    for (int b = 0; b < MC_BANDS; b++) {
        const McBand *B = &c->p.band[b];
        int type = (int)(B->type + 0.5f), cut = mc_band_is_cut(type);
        m->eq_dyn_db[b] = 0.f;
        m->eq_det_db[b] = -120.f;
        if (B->on < 0.5f) { memset(c->eq_z[b], 0, sizeof c->eq_z[b]); continue; }

        /* The detector always runs on non-cut bands so the page can show the band level against
         * Thresh even before Range is set. Full Range is reached 6 dB above Thresh. */
        float dyn = 0.f;
        if (!cut) {
            McCoef dk;
            mc_bq_design(MC_BQ_BANDPASS, c->sr, B->freq, B->q < 0.7f ? 0.7f : B->q, 0.f, &dk);
            float pk = 0.f;
            for (int i = 0; i < n; i++) {
                float a = fabsf(bq(&dk, c->det_z[b], 0.5f * (L[i] + R[i])));
                if (a > pk) pk = a;
            }
            float coef = block_coef(c, n, pk > c->eq_env[b] ? B->attack_ms : B->release_ms);
            c->eq_env[b] += (pk - c->eq_env[b]) * coef;
            float level = lin2db(c->eq_env[b]);
            m->eq_det_db[b] = level;
            dyn = B->range_db * clampf((level - B->thresh_db) / 6.f, 0.f, 1.f);
        }
        m->eq_dyn_db[b] = dyn;

        float g = cut ? 0.f : B->gain_db + dyn;
        if (!cut && fabsf(g) < 0.01f) { memset(c->eq_z[b], 0, sizeof c->eq_z[b]); continue; }
        McCoef k;
        mc_bq_design(mc_band_filter(type), c->sr, B->freq, B->q, g, &k);
        for (int i = 0; i < n; i++) {
            L[i] = bq(&k, c->eq_z[b][0], L[i]);
            R[i] = bq(&k, c->eq_z[b][1], R[i]);
        }
    }
}

/* Stock compressor model: RMS per block, hard knee, linear gain ramp across the block. */
static void comp_block(McChain *c, float *L, float *R, int n, McMeters *m)
{
    const McParams *p = &c->p;
    float g = db2lin(p->comp_gain_db);
    double ss = 0;
    for (int i = 0; i < n; i++) {
        L[i] *= g; R[i] *= g;
        ss += (double)L[i] * L[i] + (double)R[i] * R[i];
    }
    float in_db = 10.f * log10f((float)(ss / (2.0 * n)) + 1e-12f);
    float over = in_db - p->comp_thresh_db;
    float ratio = p->comp_ratio < 1.f ? 1.f : p->comp_ratio;
    float target = over > 0.f ? -over * (1.f - 1.f / ratio) : 0.f;
    float coef = block_coef(c, n, target < c->comp_db ? p->comp_attack_ms : p->comp_release_ms);
    c->comp_db += (target - c->comp_db) * coef;
    float lin = db2lin(c->comp_db), old = c->comp_lin;
    for (int i = 0; i < n; i++) {
        float gi = old + (lin - old) * (float)(i + 1) / (float)n;
        L[i] *= gi; R[i] *= gi;
    }
    c->comp_lin = lin;
    m->comp_in_db = in_db;
    m->cmp_gr_db = c->comp_db;
}

/* Stock limiter model: block peak against 0 dBFS, attack/release ramp, then the +-1.0 clamp. */
static void lim_block(McChain *c, float *L, float *R, int n, McMeters *m)
{
    const McParams *p = &c->p;
    float pk = 0.f;
    for (int i = 0; i < n; i++) {
        float a = fabsf(L[i]), b = fabsf(R[i]);
        if (a > pk) pk = a;
        if (b > pk) pk = b;
    }
    float target = pk > 1.f ? 1.f / pk : 1.f;
    float coef = block_coef(c, n, target < c->lim_gain ? p->lim_attack_ms : p->lim_release_ms);
    float newg = c->lim_gain + (target - c->lim_gain) * coef;
    if (newg > 0.99989f) newg = 1.f;         /* within 0.001 dB: unity, so a bus under 0 dBFS passes bit-exact */
    float old = c->lim_gain;
    for (int i = 0; i < n; i++) {
        float gi = old + (newg - old) * (float)(i + 1) / (float)n;
        L[i] = clampf(L[i] * gi, -1.f, 1.f);
        R[i] = clampf(R[i] * gi, -1.f, 1.f);
    }
    c->lim_gain = newg;
    m->lim_gr_db = lin2db(newg);
}

/* ---- chain ------------------------------------------------------------------------------------ */

void mc_init(McChain *c, float sr)
{
    memset(c, 0, sizeof *c);
    c->sr = sr;
    c->comp_lin = 1.f;
    c->lim_gain = 1.f;
    mc_clr_reset(&c->clr);
    mc_cmp_reset(&c->cmp);
    mc_max_reset(&c->max, sr);
    mc_default_params(&c->p);
}

void mc_set_params(McChain *c, const McParams *p) { c->p = *p; }
void mc_set_bpm(McChain *c, float bpm) { c->bpm = bpm; }
int mc_latency(const McChain *c) { return c->max.len; }

static void chunk(McChain *c, float *L, float *R, int n, McMeters *m)
{
    const McParams *p = &c->p;
    memset(m, 0, sizeof *m);
    m->comp_in_db = m->out_peak_db = -120.f;

    if (p->chain_on > 0.5f && p->eq_on > 0.5f) eq_block(c, L, R, n, m);
    else memset(c->eq_z, 0, sizeof c->eq_z);

    if (p->chain_on > 0.5f) {
        mc_clr_process(&c->clr, c->sr, p, L, R, n, &m->clr_clip);
        if (p->cmp_type < 0.5f) {
            mc_cmp_process(&c->cmp, c->sr, p, L, R, n, &m->cmp_gr_db);
            c->comp_db = 0.f; c->comp_lin = 1.f;
        } else {
            comp_block(c, L, R, n, m);
        }
        mc_max_process(&c->max, c->sr, c->bpm, p, L, R, n, &m->max_gr_db, &m->max_over_db);
        c->lim_gain = 1.f;
    } else {
        /* chain off: no master processing, only the stock limiter as a safety (as on the unit) */
        c->comp_db = 0.f; c->comp_lin = 1.f;
        lim_block(c, L, R, n, m);
    }

    float pk = 0.f;
    for (int i = 0; i < n; i++) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
    m->out_peak_db = lin2db(pk);
}

void mc_process(McChain *c, float *L, float *R, int n)
{
    McMeters acc, cm;
    int first = 1;
    for (int pos = 0; pos < n;) {
        int k = n - pos < MC_BLOCK ? n - pos : MC_BLOCK;
        chunk(c, L + pos, R + pos, k, &cm);
        if (first) { acc = cm; first = 0; }
        else {
            for (int b = 0; b < MC_BANDS; b++) {
                if (fabsf(cm.eq_dyn_db[b]) > fabsf(acc.eq_dyn_db[b])) acc.eq_dyn_db[b] = cm.eq_dyn_db[b];
                acc.eq_det_db[b] = fmaxf(acc.eq_det_db[b], cm.eq_det_db[b]);
            }
            acc.clr_clip = fmaxf(acc.clr_clip, cm.clr_clip);
            acc.cmp_gr_db = fminf(acc.cmp_gr_db, cm.cmp_gr_db);
            acc.comp_in_db = fmaxf(acc.comp_in_db, cm.comp_in_db);
            acc.max_gr_db = fminf(acc.max_gr_db, cm.max_gr_db);
            acc.max_over_db = fmaxf(acc.max_over_db, cm.max_over_db);
            acc.lim_gr_db = fminf(acc.lim_gr_db, cm.lim_gr_db);
            acc.out_peak_db = fmaxf(acc.out_peak_db, cm.out_peak_db);
        }
        pos += k;
    }
    if (!first) c->m = acc;
}

void mc_run_eq(McChain *c, float *L, float *R, int n)
{
    McMeters cm;
    float dyn[MC_BANDS] = { 0 }, det[MC_BANDS];
    for (int b = 0; b < MC_BANDS; b++) det[b] = -120.f;
    for (int pos = 0; pos < n;) {
        int k = n - pos < MC_BLOCK ? n - pos : MC_BLOCK;
        memset(&cm, 0, sizeof cm);
        if (c->p.eq_on > 0.5f) eq_block(c, L + pos, R + pos, k, &cm);
        else { memset(c->eq_z, 0, sizeof c->eq_z); for (int b = 0; b < MC_BANDS; b++) cm.eq_det_db[b] = -120.f; }
        for (int b = 0; b < MC_BANDS; b++) {
            if (fabsf(cm.eq_dyn_db[b]) > fabsf(dyn[b])) dyn[b] = cm.eq_dyn_db[b];
            det[b] = fmaxf(det[b], cm.eq_det_db[b]);
        }
        pos += k;
    }
    for (int b = 0; b < MC_BANDS; b++) { c->m.eq_dyn_db[b] = dyn[b]; c->m.eq_det_db[b] = det[b]; }
}

void mc_run_pre(McChain *c, float *L, float *R, int n)
{
    McMeters cm;
    float clip = 0.f, gr = 0.f, in = -120.f;
    for (int pos = 0; pos < n;) {
        int k = n - pos < MC_BLOCK ? n - pos : MC_BLOCK;
        memset(&cm, 0, sizeof cm);
        cm.comp_in_db = -120.f;
        mc_clr_process(&c->clr, c->sr, &c->p, L + pos, R + pos, k, &cm.clr_clip);
        if (c->p.cmp_type < 0.5f) {
            mc_cmp_process(&c->cmp, c->sr, &c->p, L + pos, R + pos, k, &cm.cmp_gr_db);
            c->comp_db = 0.f; c->comp_lin = 1.f;
        } else {
            comp_block(c, L + pos, R + pos, k, &cm);
        }
        clip = fmaxf(clip, cm.clr_clip);
        gr = fminf(gr, cm.cmp_gr_db);
        in = fmaxf(in, cm.comp_in_db);
        pos += k;
    }
    c->m.clr_clip = clip;
    c->m.cmp_gr_db = gr;
    c->m.comp_in_db = in;
}

void mc_run_max(McChain *c, float *L, float *R, int n)
{
    float gr = 0.f, over = 0.f, pk = 0.f;
    for (int pos = 0; pos < n;) {
        int k = n - pos < MC_BLOCK ? n - pos : MC_BLOCK;
        float g, o;
        mc_max_process(&c->max, c->sr, c->bpm, &c->p, L + pos, R + pos, k, &g, &o);
        gr = fminf(gr, g);
        over = fmaxf(over, o);
        for (int i = pos; i < pos + k; i++) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
        pos += k;
    }
    c->m.max_gr_db = gr;
    c->m.max_over_db = over;
    c->m.out_peak_db = lin2db(pk);
}

void mc_run_limit(McChain *c, float *L, float *R, int n)
{
    McMeters cm;
    float gr = 0.f, pk = 0.f;
    for (int pos = 0; pos < n;) {
        int k = n - pos < MC_BLOCK ? n - pos : MC_BLOCK;
        memset(&cm, 0, sizeof cm);
        lim_block(c, L + pos, R + pos, k, &cm);
        gr = fminf(gr, cm.lim_gr_db);
        for (int i = pos; i < pos + k; i++) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
        pos += k;
    }
    c->m.lim_gr_db = gr;
    c->m.out_peak_db = lin2db(pk);
}

/* ---- defaults and parameter table ------------------------------------------------------------- */

void mc_stock_params(McParams *p)
{
    static const float freq[MC_BANDS] = { 30.f, 80.f, 400.f, 2500.f, 9000.f };
    /* thresholds near each band's typical detector level on a full mix (measured on the
     * An Mir Vorbei premaster: B2 -17, B3 -22, B4 -27, B5 -29 dB mean), so Range engages */
    static const float thresh[MC_BANDS] = { -24.f, -18.f, -24.f, -30.f, -32.f };
    static const int type[MC_BANDS] = { MC_BAND_LOWCUT, MC_BAND_LOWSHELF, MC_BAND_PARA, MC_BAND_PARA,
                                        MC_BAND_HIGHSHELF };
    memset(p, 0, sizeof *p);
    for (int b = 0; b < MC_BANDS; b++) {
        McBand *B = &p->band[b];
        B->on = 1.f; B->type = (float)type[b]; B->freq = freq[b];
        B->q = type[b] == MC_BAND_LOWCUT ? 0.707f : 0.8f;
        B->thresh_db = thresh[b]; B->attack_ms = 10.f; B->release_ms = 150.f;
    }
    /* Mastering2 / PurestSquish plugin defaults: neutral */
    p->clr_scope = p->clr_skronk = p->clr_girth = p->clr_drive = 0.5f;
    p->sq_output = 1.f; p->sq_mix = 1.f;
    /* stock compressor and limiter (master.S defaults = the stock ctor) */
    p->comp_gain_db = -4.f; p->comp_thresh_db = -4.f; p->comp_ratio = 4.f;
    p->comp_attack_ms = 10.f; p->comp_release_ms = 250.f;
    p->lim_attack_ms = 4.f; p->lim_release_ms = 750.f;
    p->max_relx = 1.f;
}

void mc_default_params(McParams *p)
{
    mc_stock_params(p);
    p->chain_on = 1.f;
    p->clr_sidepass = 0.3f; p->clr_glue = 0.3f;
    p->sq_squish = 0.4f; p->sq_bassbloom = 0.3f;
    p->max_amount = 0.65f;     /* about -11 LUFS on the An Mir Vorbei premaster (Henning: -10.3) */
}

#define F(field) (unsigned)offsetof(McParams, field)
#define BAND(b, field) (unsigned)(offsetof(McParams, band) + (b) * sizeof(McBand) + offsetof(McBand, field))
#define BANDDEFS(b, N)                                                                        \
    { "b" #N "_on", "B" #N " On", "", 0, 0, 1, BAND(b, on), MC_KIND_TOGGLE },                 \
    { "b" #N "_type", "B" #N " Type", "", "LowCut|LowShelf|Para|HighShelf|HighCut", 0, 4,      \
      BAND(b, type), MC_KIND_CHOICE },                                                        \
    { "b" #N "_freq", "B" #N " Freq", "Hz", 0, 20, 20000, BAND(b, freq), MC_KIND_LOG },       \
    { "b" #N "_q", "B" #N " Q", "", 0, 0.3f, 8, BAND(b, q), MC_KIND_FLOAT },                  \
    { "b" #N "_gain", "B" #N " Gain", "dB", 0, -12, 12, BAND(b, gain_db), MC_KIND_FLOAT },     \
    { "b" #N "_thresh", "B" #N " Thresh", "dB", 0, -60, 0, BAND(b, thresh_db), MC_KIND_FLOAT },\
    { "b" #N "_range", "B" #N " Range", "dB", 0, -12, 12, BAND(b, range_db), MC_KIND_FLOAT },  \
    { "b" #N "_atk", "B" #N " Attack", "ms", 0, 1, 200, BAND(b, attack_ms), MC_KIND_LOG },     \
    { "b" #N "_rel", "B" #N " Release", "ms", 0, 10, 2000, BAND(b, release_ms), MC_KIND_LOG }

static const McParamDef defs[] = {
    { "chain_on", "Chain", "", 0, 0, 1, F(chain_on), MC_KIND_TOGGLE },
    { "eq_on", "EQ On", "", 0, 0, 1, F(eq_on), MC_KIND_TOGGLE },
    BANDDEFS(0, 1), BANDDEFS(1, 2), BANDDEFS(2, 3), BANDDEFS(3, 4), BANDDEFS(4, 5),
    { "clr_sidepass", "Sidepass", "", 0, 0, 1, F(clr_sidepass), MC_KIND_FLOAT },
    { "clr_glue", "Glue", "", 0, 0, 1, F(clr_glue), MC_KIND_FLOAT },
    { "clr_scope", "Scope", "", 0, 0, 1, F(clr_scope), MC_KIND_FLOAT },
    { "clr_skronk", "Skronk", "", 0, 0, 1, F(clr_skronk), MC_KIND_FLOAT },
    { "clr_girth", "Girth", "", 0, 0, 1, F(clr_girth), MC_KIND_FLOAT },
    { "clr_drive", "Drive", "", 0, 0, 1, F(clr_drive), MC_KIND_FLOAT },
    { "cmp_type", "Type", "", "Squish|Stock", 0, 1, F(cmp_type), MC_KIND_CHOICE },
    { "sq_squish", "Squish", "", 0, 0, 1, F(sq_squish), MC_KIND_FLOAT },
    { "sq_bassbloom", "BassBlm", "", 0, 0, 1, F(sq_bassbloom), MC_KIND_FLOAT },
    { "sq_output", "Output", "", 0, 0, 1, F(sq_output), MC_KIND_FLOAT },
    { "sq_mix", "Dry/Wet", "", 0, 0, 1, F(sq_mix), MC_KIND_FLOAT },
    { "c_thresh", "Thresh", "dB", 0, -48, 0, F(comp_thresh_db), MC_KIND_FLOAT },
    { "c_ratio", "Ratio", ":1", 0, 1, 20, F(comp_ratio), MC_KIND_LOG },
    { "c_atk", "Attack", "ms", 0, 0.5f, 500, F(comp_attack_ms), MC_KIND_LOG },
    { "c_rel", "Release", "ms", 0, 10, 2000, F(comp_release_ms), MC_KIND_LOG },
    { "c_gain", "Gain", "dB", 0, -12, 12, F(comp_gain_db), MC_KIND_FLOAT },
    { "max_amount", "Maximize", "", 0, 0, 1, F(max_amount), MC_KIND_FLOAT },
    { "max_relx", "Rel x", "", "x0.5|x1|x2", 0, 2, F(max_relx), MC_KIND_CHOICE },
    { "l_atk", "L Attack", "ms", 0, 0.5f, 50, F(lim_attack_ms), MC_KIND_LOG },
    { "l_rel", "L Release", "ms", 0, 10, 3000, F(lim_release_ms), MC_KIND_LOG },
};

int mc_param_count(void) { return (int)(sizeof defs / sizeof defs[0]); }
const McParamDef *mc_param(int i) { return i >= 0 && i < mc_param_count() ? &defs[i] : 0; }

int mc_param_find(const char *id)
{
    for (int i = 0; i < mc_param_count(); i++)
        if (strcmp(defs[i].id, id) == 0) return i;
    return -1;
}

float mc_param_get(const McParams *p, int i)
{
    return *(const float *)((const char *)p + defs[i].off);
}

void mc_param_set(McParams *p, int i, float v)
{
    *(float *)((char *)p + defs[i].off) = clampf(v, defs[i].mn, defs[i].mx);
}

/* ---- demo loop -------------------------------------------------------------------------------- */

void mc_demo_init(McDemo *d, float sr)
{
    memset(d, 0, sizeof *d);
    d->sr = sr; d->noise = 12345u;
}

static float rnd(McDemo *d)
{
    d->noise = d->noise * 1664525u + 1013904223u;
    return (float)(d->noise >> 8) * (2.f / 16777216.f) - 1.f;
}

void mc_demo_fill(McDemo *d, float *L, float *R, int n)
{
    const double TWO_PI = 6.283185307179586;
    for (int i = 0; i < n; i++) {
        double beat = d->t * 2.0 / d->sr;                 /* 120 bpm */
        double sec = fmod(beat, 1.0) * 0.5;               /* seconds since this beat */
        int idx = (int)fmod(beat, 4.0);
        double s8 = fmod(beat * 2.0, 1.0) * 0.25;         /* seconds since this eighth */

        float kick = (float)(sin(TWO_PI * (45.0 * sec + 2.5 * (1.0 - exp(-30.0 * sec)))) * exp(-sec * 8.0)) * 0.9f;
        float snare = (idx & 1) ? rnd(d) * (float)exp(-sec * 14.0) * 0.5f : 0.f;
        float h = rnd(d);
        d->hat_lp += 0.3f * (h - d->hat_lp);
        float hat = (h - d->hat_lp) * (float)exp(-s8 * 60.0) * 0.3f;
        d->bass_phase += 55.0 / d->sr;
        if (d->bass_phase >= 1.0) d->bass_phase -= 1.0;
        float bass = (float)(2.0 * d->bass_phase - 1.0) * 0.28f * (float)(0.5 + 0.5 * exp(-sec * 3.0));

        float mid = kick + bass + snare;
        L[i] = mid + hat;
        R[i] = mid - hat * 0.6f;
        d->t += 1.0;
    }
}
