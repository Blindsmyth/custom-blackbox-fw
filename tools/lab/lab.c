/* Warp Lab: the firmware stretch code (tools/stretch) compiled to WebAssembly,
 * so algorithms can be A/B'd live in the browser. Same C as the cave; this file
 * only holds the sample, the voices and a stock-Stretch emulation.
 *
 * Build: tools/lab/build.sh. JS writes the clip into lab_in_l/lab_in_r, calls
 * lab_load, then lab_render per 1024-sample block. */
#include <stdint.h>

#include "capi.h"
#include "smooth.h"
#include "vari.h"
#include "beats.h"
#include "deluge.h"
#include "granular.h"

#define EXPORT(name) __attribute__((export_name(#name)))

#define MAX_FRAMES (48000 * 90)
#define BLOCK_MAX  4096

enum { ALGO_STOCK, ALGO_GRAIN, ALGO_SMOOTH, ALGO_CAPI, ALGO_VARI, ALGO_DRY, ALGO_SYNC, ALGO_BEATS, ALGO_SLICE, ALGO_DELUGE, ALGO_N };

static void make_grid(void);
static void merge_slices(void);
int lab_slices(float sensitivity);

static float in_l[MAX_FRAMES], in_r[MAX_FRAMES];
static DelugeVoice deluge;
static GranVoice gran;
/* Granular (the stock pad engine, docs/map-319.md) and its tempo-synced variant. */
#define ALGO_GRAN      30
#define ALGO_GRAN_SYNC 31
static int gran_p[7] = { 82, 250, 0, 0, 1000, 0, 0 };
static uint8_t deluge_pix[MAX_FRAMES / DELUGE_PIX + 1];
static int deluge_n_pix;
static int length, stereo;
static float out_l[BLOCK_MAX], out_r[BLOCK_MAX];

static SmoothVoice smooth;
static CapiVoice capi;
static VariVoice vari;
static uint32_t vari_marks[VARI_MAX_MARKS];
static int vari_n;
static float vari_scratch[VARI_SCRATCH_N];

/* clang lowers struct copies and zeroing loops to these. */
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

static int read_clip(void *sample, float *dest, int count, int pos, int chan)
{
    const float *src = chan ? in_r : in_l;
    int i;
    (void)sample;
    for (i = 0; i < count; i++)
        dest[i] = src[pos + i];
    return count;
}

/* ── Stock Stretch emulation: Hann OLA, no alignment (proto's ola_grain) ──── */

typedef struct Ola {
    int win, hop;
    float pos;
    int emitted, have;
    float w[2048];
    float acc_l[2048], acc_r[2048];
} Ola;

static Ola ola_stock, ola_grain;

static void ola_init(Ola *o, int win, float cos_step)
{
    /* Hann by the cosine recurrence: no libm in this build. */
    float c0 = 1.f, c1 = cos_step, k = 2.f * cos_step;
    int i;
    memset(o, 0, sizeof *o);
    o->win = win;
    o->hop = win / 2;
    o->emitted = o->hop;
    for (i = 0; i < win; i++) {
        float c = i == 0 ? c0 : c1;
        o->w[i] = 0.5f * (1.f - c);
        if (i > 0) {
            float n = k * c1 - c0;
            c0 = c1;
            c1 = n;
        }
    }
}

static int wrap(int p)
{
    p %= length;
    return p < 0 ? p + length : p;
}

static void ola_process(Ola *o, float *dl, float *dr, int n, float ratio)
{
    int out = 0;
    while (out < n) {
        int i, take;
        if (!o->have || o->emitted >= o->hop) {
            int ip = (int)o->pos;
            if (o->have) {
                for (i = 0; i < o->hop; i++) {
                    o->acc_l[i] = o->acc_l[i + o->hop];
                    o->acc_r[i] = o->acc_r[i + o->hop];
                }
                for (i = o->hop; i < o->win; i++)
                    o->acc_l[i] = o->acc_r[i] = 0.f;
            }
            for (i = 0; i < o->win; i++) {
                int p = wrap(ip + i);
                o->acc_l[i] += in_l[p] * o->w[i];
                o->acc_r[i] += in_r[p] * o->w[i];
            }
            o->pos += (float)o->hop * ratio;
            while (o->pos >= (float)length)
                o->pos -= (float)length;
            o->have = 1;
            o->emitted = 0;
        }
        take = o->hop - o->emitted;
        if (take > n - out)
            take = n - out;
        for (i = 0; i < take; i++) {
            dl[out + i] += o->acc_l[o->emitted + i];
            dr[out + i] += o->acc_r[o->emitted + i];
        }
        o->emitted += take;
        out += take;
    }
}

/* ── Stock Stretch with Sync (FUN_08064a94 / FUN_08065550 in 3.1.9) ────────
 * The clip is cut into N = beats x per-beat segments of src_seg samples
 * (clip+0x1FC). On every segment boundary of the host tempo a grain starts at
 * segment k of the source and plays at 1x for one host segment (clip+0x204):
 * fade in 256, hold out_seg - 256, fade out 256, so neighbours overlap by 256.
 * Up to 4 grains (clip+0x30 + i*0x70). The launch grain has no fade-in.
 * Slower than the clip, a grain runs past its own segment into the next one,
 * which the next grain then plays again (the stutter). */

#define SYNC_FADE  256
#define SYNC_GRAINS 4

typedef struct SyncGrain {
    int active;
    float pos;      /* source position */
    int age;        /* output samples since start */
    int fade_in;
    int hold;
} SyncGrain;

static struct {
    SyncGrain g[SYNC_GRAINS];
    double host;    /* host position in source samples */
    int seg;        /* segment of the newest grain */
    int started;
} sync;
static float sync_clip_bpm = 120.f;
static int sync_per_beat = 4;

EXPORT(lab_sync_params)
void lab_sync_params(float clip_bpm, int per_beat)
{
    sync_clip_bpm = clip_bpm > 1.f ? clip_bpm : 120.f;
    sync_per_beat = per_beat > 0 ? per_beat : 4;
    make_grid();
    merge_slices();
}

static void sync_start(int k, float src_seg, float out_seg, int launch)
{
    int i, slot = 0, oldest = -1;
    for (i = 0; i < SYNC_GRAINS; i++) {
        if (!sync.g[i].active) {
            slot = i;
            oldest = -2;
            break;
        }
        if (oldest < 0 || sync.g[i].age > sync.g[oldest].age)
            oldest = i;
    }
    if (oldest >= 0)
        slot = oldest;
    sync.g[slot].active = 1;
    sync.g[slot].pos = (float)k * src_seg;
    sync.g[slot].age = 0;
    sync.g[slot].fade_in = launch ? 0 : SYNC_FADE;
    sync.g[slot].hold = (int)out_seg - SYNC_FADE + (launch ? SYNC_FADE : 0);
    if (sync.g[slot].hold < 0)
        sync.g[slot].hold = 0;
}

static void sync_process(float *dl, float *dr, int n, float ratio)
{
    float beats = (float)length / 48000.f * sync_clip_bpm / 60.f;
    int segs = (int)(beats * (float)sync_per_beat + 0.5f), i, j;
    float src_seg, out_seg;
    if (segs < 1)
        segs = 1;
    src_seg = (float)length / (float)segs;
    out_seg = src_seg / ratio;
    for (i = 0; i < n; i++) {
        int k = (int)(sync.host / (double)src_seg + 1e-9);
        float l = 0.f, r = 0.f;
        if (!sync.started || k != sync.seg) {
            sync.seg = k;
            sync_start(k % segs, src_seg, out_seg, !sync.started);
            sync.started = 1;
        }
        for (j = 0; j < SYNC_GRAINS; j++) {
            SyncGrain *g = &sync.g[j];
            float env;
            int a, p;
            if (!g->active)
                continue;
            a = g->age;
            if (a < g->fade_in)
                env = (float)a / (float)g->fade_in;
            else if (a < g->fade_in + g->hold)
                env = 1.f;
            else if (a < g->fade_in + g->hold + SYNC_FADE)
                env = 1.f - (float)(a - g->fade_in - g->hold) / (float)SYNC_FADE;
            else {
                g->active = 0;
                continue;
            }
            p = wrap((int)g->pos);
            l += in_l[p] * env;
            r += in_r[p] * env;
            g->pos += 1.f;
            g->age++;
        }
        dl[i] += l;
        dr[i] += r;
        sync.host += (double)ratio;
        if (sync.host >= (double)length)
            sync.host -= (double)length;
    }
}

/* ── Beats (Sync grid) and Slice (onset markers): tools/stretch/beats.c ──── */

#define MAX_SEGS 1024
static BeatsVoice beats;
static int32_t grid[MAX_SEGS], slices[MAX_SEGS], raw_slices[MAX_SEGS];
static int n_grid, n_slices, n_raw;
static float slice_gap = 2.5f;  /* fill gaps longer than this many Sync steps; 0 = off */

/* Slice segments: detected markers, grid points in long gaps. */
static void merge_slices(void)
{
    float step = n_grid > 0 ? (float)length / (float)n_grid : 0.f;
    n_slices = beats_fill_gaps(raw_slices, n_raw, length, step, slice_gap * step,
                               slices, MAX_SEGS);
}
static int beats_fill = BEATS_LOOP, beats_attack = 256, beats_cut = 512, beats_xf = 1024, beats_pp = 2400;
static int beats_glen = 2400, beats_dens = 4;
static float slice_sens = 4.f;

static void make_grid(void)
{
    float beats_n = (float)length / 48000.f * sync_clip_bpm / 60.f, seg;
    int segs = (int)(beats_n * (float)sync_per_beat + 0.5f), k;
    if (segs < 1)
        segs = 1;
    if (segs > MAX_SEGS)
        segs = MAX_SEGS;
    seg = (float)length / (float)segs;
    for (k = 0; k < segs; k++)
        grid[k] = (int32_t)((float)k * seg + 0.5f);
    n_grid = segs;
}

/* Re-detect slices. min gap: a 1/32 at the clip tempo. */
EXPORT(lab_slices) int lab_slices(float sensitivity)
{
    int gap = (int)(48000.f * 60.f / sync_clip_bpm / 8.f);
    slice_sens = sensitivity;
    n_raw = beats_detect(0, length, stereo, read_clip, raw_slices, MAX_SEGS, sensitivity, gap);
    merge_slices();
    return n_raw;
}

EXPORT(lab_slice_gap) int lab_slice_gap(float steps)
{
    slice_gap = steps;
    merge_slices();
    return n_slices;
}

EXPORT(lab_slice_count) int lab_slice_count(void) { return n_slices; }

EXPORT(lab_slice_at) int lab_slice_at(int i) { return i >= 0 && i < n_slices ? slices[i] : -1; }

EXPORT(lab_beats_params) void lab_beats_params(int fill, int attack, int cut, int xf, int pp,
                                               int grain_len, int density)
{
    beats_glen = grain_len >= 64 ? grain_len : 2400;
    beats_dens = density >= 1 && density <= BEATS_CLOUD ? density : 4;
    beats_pp = pp > 1 ? pp : 2;
    beats_fill = fill;
    beats_attack = attack > 0 ? attack : 1;
    beats_cut = cut > 0 ? cut : 1;
    beats_xf = xf > 0 ? xf : 1;
}

static void beats_reset(void)
{
    beats_init(&beats);
    beats.fill = beats_fill;
    beats.attack = beats_attack;
    beats.cut = beats_cut;
    beats.loop_xf = beats_xf;
    beats.pp_span = beats_pp;
    beats.grain_len = beats_glen;
    beats.density = beats_dens;
}

/* Unstretched loop, for reference. */
static float dry_pos;

/* ── Exports ─────────────────────────────────────────────────────────────── */

EXPORT(lab_in_l) float *lab_in_l(void) { return in_l; }
EXPORT(lab_in_r) float *lab_in_r(void) { return in_r; }
EXPORT(lab_out_l) float *lab_out_l(void) { return out_l; }
EXPORT(lab_out_r) float *lab_out_r(void) { return out_r; }
EXPORT(lab_max_frames) int lab_max_frames(void) { return MAX_FRAMES; }

/* The clip is in lab_in_l / lab_in_r. Returns the Vari mark count. */
EXPORT(lab_load) int lab_load(int frames, int is_stereo)
{
    int i;
    length = frames < MAX_FRAMES ? frames : MAX_FRAMES;
    stereo = is_stereo ? 1 : 0;
    if (!stereo)
        for (i = 0; i < length; i++)
            in_r[i] = in_l[i];
    vari_n = vari_analyze(0, length, stereo, read_clip, vari_marks, VARI_MAX_MARKS,
                          vari_scratch, VARI_SCRATCH_N);
    make_grid();
    lab_slices(slice_sens);
    deluge_n_pix = length / DELUGE_PIX + 1;
    deluge_perc(0, length, stereo, read_clip, deluge_pix, deluge_n_pix);
    return vari_n;
}

/* Capi tunables; take effect on the next reset. */
static int capi_leash = CAPI_LEASH, capi_onset = 1;
static float capi_fade = CAPI_FADE, capi_eps = CAPI_EPS, capi_thresh = CAPI_THRESH;

/* Gran tab, stock ranges: Grain Size, Density, Scatter, Window, Pan Rnd 0..1000, Speed 0..2000,
 * Root Note 0..127 (0 = none). Take effect on the next reset. */
EXPORT(lab_gran_params)
void lab_gran_params(int size, int density, int scatter, int window, int speed, int panrnd, int root)
{
    gran_p[0] = size;
    gran_p[1] = density;
    gran_p[2] = scatter;
    gran_p[3] = window;
    gran_p[4] = speed;
    gran_p[5] = panrnd;
    gran_p[6] = root;
}

EXPORT(lab_capi_params)
void lab_capi_params(int leash, float fade, float eps, float thresh, int onset)
{
    capi_leash = leash;
    capi_fade = fade;
    capi_eps = eps;
    capi_thresh = thresh;
    capi_onset = onset;
}

/* Render n (<= BLOCK_MAX) samples of `algo` into lab_out_l / lab_out_r. */
EXPORT(lab_render) int lab_render(int algo, int n, float ratio, int reset)
{
    int i;
    if (length <= 0 || n <= 0)
        return 0;
    if (n > BLOCK_MAX)
        n = BLOCK_MAX;
    for (i = 0; i < n; i++)
        out_l[i] = out_r[i] = 0.f;
    switch (algo) {
    case ALGO_STOCK:
        if (reset)
            ola_init(&ola_stock, 256, 0.99969881869620425f);
        ola_process(&ola_stock, out_l, out_r, n, ratio);
        break;
    case ALGO_GRAIN:
        if (reset)
            ola_init(&ola_grain, 2048, 0.99999529380957619f);
        ola_process(&ola_grain, out_l, out_r, n, ratio);
        break;
    case ALGO_SMOOTH:
        if (reset)
            smooth_init(&smooth);
        smooth_process(&smooth, 0, length, stereo, read_clip, out_l, out_r, n, ratio, reset);
        break;
    case ALGO_CAPI:
        if (reset) {
            capi_init(&capi);
            capi.leash = capi_leash;
            capi.fade = capi_fade;
            capi.eps = capi_eps;
            capi.thresh = capi_thresh;
            capi.onset = capi_onset;
        }
        capi_process(&capi, 0, length, stereo, read_clip, out_l, out_r, n, ratio, reset);
        break;
    case ALGO_VARI:
        if (reset)
            vari_init(&vari);
        vari_process(&vari, 0, length, stereo, read_clip, vari_marks, vari_n,
                     out_l, out_r, n, ratio, reset);
        break;
    case ALGO_BEATS:
    case ALGO_SLICE:
        if (reset)
            beats_reset();
        if (algo == ALGO_BEATS)
            beats_process(&beats, 0, length, stereo, read_clip, grid, n_grid,
                          out_l, out_r, n, ratio, reset);
        else
            beats_process(&beats, 0, length, stereo, read_clip, slices, n_slices,
                          out_l, out_r, n, ratio, reset);
        break;
    case ALGO_DELUGE:
        deluge_process(&deluge, 0, length, stereo, read_clip, deluge_pix, deluge_n_pix,
                       out_l, out_r, n, ratio, reset);
        break;
    case ALGO_GRAN:
    case ALGO_GRAN_SYNC:
        if (reset) {
            gran_init(&gran);
            gran_set(&gran, gran_p[0], gran_p[1], gran_p[2], gran_p[3], gran_p[4], gran_p[5], gran_p[6],
                     algo == ALGO_GRAN_SYNC);
        }
        gran_process(&gran, 0, length, stereo, read_clip, out_l, out_r, n, ratio, reset);
        break;
    case ALGO_SYNC:
        if (reset)
            memset(&sync, 0, sizeof sync);
        sync_process(out_l, out_r, n, ratio);
        break;
    default:
        if (reset)
            dry_pos = 0.f;
        for (i = 0; i < n; i++) {
            int p = wrap((int)dry_pos);
            out_l[i] = in_l[p];
            out_r[i] = in_r[p];
            dry_pos += 1.f;
            if (dry_pos >= (float)length)
                dry_pos -= (float)length;
        }
        break;
    }
    return n;
}
