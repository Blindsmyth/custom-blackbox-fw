/* Blackbox master chain: EQ -> CLR -> CMP -> MAX.
 *
 *   EQ   4-band dynamic EQ (stand-in for the EQ tab; independent of the chain switch)
 *   CLR  Airwindows Mastering2, float port, no dither                     (mc_clr.c)
 *   CMP  Airwindows PurestSquish float port, or the stock compressor model (mc_cmp.c / comp_block)
 *   MAX  one-knob look-ahead maximizer with a tempo-synced release        (mc_max.c)
 *
 * Chain off = the stock path: stock compressor model + stock block limiter model
 * (docs/map-319.md "Master compressor"), exactly as before.
 *
 * Plain C, float, stereo, no allocation; written to be ported into the firmware unchanged.
 * Every McParams / McBand field is a float so hosts can address them by offset.
 */
#ifndef MC_DSP_H
#define MC_DSP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MC_BANDS 5
#define MC_BLOCK 128
#define MC_MAX_LOOKAHEAD 72          /* 1.5 ms at 48 kHz */
#define MC_MAX_CEILING 0.96605088f   /* -0.3 dBFS */

/* band types (McBand.type); defaults: low cut, low shelf, para, para, high shelf */
enum { MC_BAND_LOWCUT, MC_BAND_LOWSHELF, MC_BAND_PARA, MC_BAND_HIGHSHELF, MC_BAND_HIGHCUT };

typedef struct {
    float on, type;
    float freq, q, gain_db;        /* static filter; cut types ignore gain and the dynamics */
    float thresh_db, range_db;     /* dynamic: range < 0 cuts above thresh, > 0 boosts */
    float attack_ms, release_ms;
} McBand;

typedef struct {
    float chain_on;                                /* MAX page: off = stock comp + stock limiter */
    float eq_on;
    McBand band[MC_BANDS];
    /* CLR: Mastering2 controls, 0..1 as in the plugin */
    float clr_sidepass, clr_glue, clr_scope, clr_skronk, clr_girth, clr_drive;
    /* CMP */
    float cmp_type;                                /* 0 = Squish, 1 = Stock */
    float sq_squish, sq_bassbloom, sq_output, sq_mix;   /* PurestSquish controls, 0..1 */
    float comp_gain_db, comp_thresh_db, comp_ratio, comp_attack_ms, comp_release_ms; /* stock */
    /* MAX */
    float max_amount;                              /* 0..1 -> 0..+12 dB into the ceiling */
    float max_relx;                                /* 0, 1, 2 -> release x0.5, x1, x2 */
    /* stock limiter, used only with the chain off */
    float lim_attack_ms, lim_release_ms;
} McParams;

/* Live readout for the UI; covers the whole last mc_process call (worst case per field). */
typedef struct {
    float eq_dyn_db[MC_BANDS];
    float eq_det_db[MC_BANDS];     /* detector level per band (what Thresh is compared with) */
    float clr_clip;               /* 1 if CLR's soft clip engaged */
    float cmp_gr_db;               /* <= 0 */
    float comp_in_db;              /* level into the compressor (stock type / chain off) */
    float max_gr_db;               /* <= 0 */
    float max_over_db;             /* how far the safety clamp had to pull (should stay 0) */
    float lim_gr_db;               /* stock limiter, chain off */
    float out_peak_db;
} McMeters;

typedef struct { float b0, b1, b2, a1, a2; } McCoef;
enum { MC_BQ_PEAK = 0, MC_BQ_LSHELF = 1, MC_BQ_HSHELF = 2, MC_BQ_BANDPASS = 3, MC_BQ_HIGHPASS = 4,
       MC_BQ_LOWPASS = 5 };

typedef struct {
    float air[32], kalM[24], kalS[24];
    float iirA, iirB, iirC;
    int fpFlip;
    float lastSinew[2];
    float lastSample[2], intermediate[2][17];
    int wasPos[2], wasNeg[2];
} McClr;

typedef struct {
    float muSpeed[5][2], muCoef[5][2], iir[5][2];
    float lastCoef[4][2], merged[2];
    int count, fpFlip;
} McCmp;

typedef struct {
    float delay[2][MC_MAX_LOOKAHEAD];
    float dq_val[MC_MAX_LOOKAHEAD + 2];      /* sliding-window minimum: monotonic queue */
    unsigned short dq_t[MC_MAX_LOOKAHEAD + 2];
    int dq_head, dq_tail;
    unsigned short t;
    float box[MC_MAX_LOOKAHEAD];             /* box filter over the held minimum */
    float box_sum;
    int pos, box_pos, len;
    float gain;
} McMax;

typedef struct {
    float sr, bpm;
    McParams p;
    McMeters m;
    float eq_z[MC_BANDS][2][2], det_z[MC_BANDS][2], eq_env[MC_BANDS];
    float comp_db, comp_lin, lim_gain;
    McClr clr;
    McCmp cmp;
    McMax max;
} McChain;

void mc_init(McChain *c, float sample_rate);
void mc_set_params(McChain *c, const McParams *p);
void mc_set_bpm(McChain *c, float bpm);       /* <= 0 or out of 20..400 = unknown */
void mc_process(McChain *c, float *left, float *right, int n);
int  mc_latency(const McChain *c);            /* samples of delay with the chain on */

/* The chain one stage at a time, for a host that has other processing between them (the
 * Blackbox: the DJ FX sit between CMP and MAX). Each runs in MC_BLOCK pieces and fills only its
 * own c->m fields. mc_run_pre is CLR then CMP (Squish, or the stock compressor model). */
void mc_run_eq(McChain *c, float *left, float *right, int n);
void mc_run_pre(McChain *c, float *left, float *right, int n);
void mc_run_max(McChain *c, float *left, float *right, int n);
/* The chain off: only the stock limiter model (block peak against 0 dBFS, lim_attack / release,
 * then the +-1.0 clamp), as a safety. */
void mc_run_limit(McChain *c, float *left, float *right, int n);

/* stages, usable on their own (reference checks, firmware) */
void mc_clr_reset(McClr *s);
void mc_clr_process(McClr *s, float sr, const McParams *p, float *L, float *R, int n, float *clipped);
void mc_cmp_reset(McCmp *s);
void mc_cmp_process(McCmp *s, float sr, const McParams *p, float *L, float *R, int n, float *gr_db);
void mc_max_reset(McMax *s, float sr);
void mc_max_process(McMax *s, float sr, float bpm, const McParams *p, float *L, float *R, int n,
                    float *gr_db, float *over_db);

void mc_bq_design(int type, float sr, float freq, float q, float gain_db, McCoef *out);
int mc_band_filter(int band_type);      /* McBand.type -> MC_BQ_* */
int mc_band_is_cut(int band_type);
float mc_bq_mag_db(const McCoef *c, float freq, float sr);

/* defaults: chain on, starting values chosen against the reference renders */
void mc_default_params(McParams *out);
/* the stock master as shipped: chain off, stock compressor and limiter settings */
void mc_stock_params(McParams *out);

/* Parameter table shared by the plugin and mc_proto. */
enum { MC_KIND_FLOAT, MC_KIND_LOG, MC_KIND_TOGGLE, MC_KIND_CHOICE };
typedef struct {
    const char *id, *name, *unit, *choices;   /* choices: "A|B|C" for MC_KIND_CHOICE */
    float mn, mx;
    unsigned off;
    int kind;
} McParamDef;
int mc_param_count(void);
const McParamDef *mc_param(int i);
int mc_param_find(const char *id);
float mc_param_get(const McParams *p, int i);
void mc_param_set(McParams *p, int i, float v);

/* Built-in test loop (kick, snare, hat, bass at 120 bpm). */
typedef struct { double t, bass_phase; unsigned noise; float sr; float hat_lp; } McDemo;
void mc_demo_init(McDemo *d, float sr);
void mc_demo_fill(McDemo *d, float *left, float *right, int n);

#ifdef __cplusplus
}
#endif
#endif
