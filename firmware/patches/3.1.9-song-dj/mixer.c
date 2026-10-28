/* Song DJ mixer (3.1.T): an audio loop of the master that keeps playing through a preset load, a
 * crossfader between it and the project, a DJ filter on each side, and a global BPM. Design and as
 * built: docs/song-dj.md "Mixer".
 *
 * The master is tapped after the compressor and before the limiter (mx_comp_tap, inside
 * FUN_0804EDDC), or where both render paths meet when the compressor is off (mx_bus_tap). The DJ FX
 * are moved there too: stock runs them right after the master EQ, before the compressor (mx_eq).
 * Per block:
 *   x    = the project (bus 0), compressed, without the DJ FX
 *   ring <- x, while recording (int16 stereo in SDRAM, one tick anchor per 128-frame block)
 *   P    = filtP(x) * gR, L = filtL(loop) * gL, gL^2 + gR^2 = 1 (gR = 1 without a loop)
 *   out  = P and L through FX A then FX B, each slot taking what the DJ FX screen routes to it
 *          (fx_run), then the limiter
 * The loop is a window [A, A + len) of recorded transport ticks, read at (t - B) mod len: it is
 * mapped by tick, so it stays on the global bar grid and repitches when the BPM changes.
 *
 * The ring is borrowed from stock's sample cache only while it is needed: from the DJ row's Loop on
 * until neither a DJ loop nor an audio loop is left. 131 cache entries (64 KB each: 128 for the audio,
 * 2 for the tick anchors, 1 scratch) are taken in pcmStreamer's own task (mx_stream_poll) the way
 * stock takes one for a block (FUN_08071D64), marked reserved (state 3, no wave), and given back as
 * stock frees them. The rest of the time the cache is stock's 615 entries.
 *
 * Capture (BACK with the DJ row's Loop on, dj.c): the DJ loop is switched off at once, so the project
 * plays its pass to the end W and on past it. The audio loop is the last pass [W - L, W), already in
 * the ring; from W it plays and the crossfader is fully left. The ring keeps recording the project's
 * continuation (until 16 bars past A or a preset load), so a bigger size plays the bars that really
 * followed. BACK again erases it: the crossfader goes back right and the ring records again. The
 * Loop button meanwhile loops the project as ever (dj.c), and the loop knob sizes both loops.
 */

#define MX_BASE      0x38007A60u   /* SRAM4: after DjState (0x38007400-0x38007A58), below */
#define MX_END       0x38008000u   /* Bungee's grain input */
#define MX_MAGIC     0x31584D53u   /* 'SMX1' */
#define BLOCK        128
#define RING_BLOCKS  16384u        /* 43.7 s at 48 kHz: 16 bars down to ~88 BPM */
#define RING_MASK    (RING_BLOCKS - 1)
#define RING_FRAMES  (RING_BLOCKS * BLOCK)
#define CHUNK_FR     8192u         /* int16 stereo frames in one 32 KB half (L or R) of a cache entry */
#define ANC_PER      4096u         /* anchors (int64) in one 32 KB half */
#define E_AUDIO      128u          /* cache entries for the audio: 128 x 2 x 8192 frames */
#define E_ANC        128u          /* entries 128, 129: the anchors */
#define E_SCRATCH    130u          /* entry 130: this block's loop audio (lp / rp) */
#define NENT         131u
#define PEAKS_OFF    (2 * BLOCK * 4)   /* scratch half 0: lp / rp, then one peak byte per ring block */
#define PROJ_N       4096u     /* project peaks kept (dj.c's buffer): ~43.7 s, the ring's span */
#define PROJ_PARTS   4         /* 128-frame parts per project peak: ~10.7 ms */
#define CACHE        0x2400A9C0u   /* sample manager (pcmStreamer's object): 615 entries of 0x1C */
#define CACHE_N      0x267u
#define C_LBUF       0x04          /* entry: float L[8192] */
#define C_RBUF       0x08          /* entry: float R[8192] */
#define C_BLOCK      0x0C
#define C_STAMP      0x10
#define C_FRAMES     0x14
#define C_WAVE       0x18          /* wave slot, 0xFFFF = none */
#define C_NEXT       0x1C          /* u16: next entry of a multi-pair block */
#define C_PAIR       0x1E          /* byte: pair index */
#define C_STATE      0x1F          /* byte: 0 free, 1 loaded, 2 recorded, 3 reserved */
#define H_NONE       0             /* have: no ring */
#define H_READY      2             /*       borrowed, in use */
#define H_DRAIN      3             /*       giving back once the audio task has stopped using it */
#define MAX_TICKS    61440         /* 16 bars */
#define TQ_SHIFT     16            /* fixed-point ticks: 1 << 16 per tick */
#define TQ_ONE       (1LL << TQ_SHIFT)
#define RESYNC_TQ    (4 * TQ_ONE)  /* own tick further than this from the engine's: take the engine's */
#define SEAM         96            /* samples of crossfade at the loop's seam (2 ms) */
#define DEAD         8             /* filter knob -8..8 of -1000..1000: off */
#define FILT_Q       0.8f
#define XF_DEAD      0.05f         /* crossfader travel at each end with the other side silent */
#define ID_BPMG      0x1D8         /* Tools -> Clock "BPM:" Project / Global */
#define FX_N         512           /* frames dj.c's FX buffers hold (the audio task's blocks are 256) */
#define SE_BUS0      0x150E4       /* FUN_0805F37C(SE, 0): bus 0 {cap, n, L, R, silent, stereo} */
#define SE_FX        0x28D90       /* DJ FX container (FUN_0806E244): the master EQ node's next stage */
#define FXC_SLOTS    0x20          /* container: slots of 0x2C, processed in order */
#define FXC_SLOT_SZ  0x2C
#define FXC_COUNT    0x180         /* u16 */
#define FXC_BYPASS   0x182         /* byte: stock then skips the slots */
#define R_PROJ       1             /* route: the project into the slot */
#define R_LOOP       2             /*        the audio loop */

#define ST_IDLE      0             /* no audio loop: the ring records */
#define ST_ARMED     1             /* captured: waiting for the DJ loop's end W */
#define ST_HELD      2             /* playing */

typedef struct {
    unsigned magic;           /* +0x00 */
    /* written by the UI task */
    unsigned cap_seq;         /* +0x04 one per BACK capture */
    unsigned cap_off;         /* +0x08 the DJ loop_seq that switched the DJ loop off for it */
    int cap_len;              /* +0x0C the DJ loop at the press: length, ticks */
    long long cap_a;          /* +0x10 its start, transport ticks */
    unsigned clr_seq;         /* +0x18 one per erase (BACK with an audio loop) */
    unsigned size_seq;        /* +0x1C one per size change */
    int size_req;             /* +0x20 ticks */
    unsigned xf_seq;          /* +0x24 one per crossfader move */
    short xf_ui;              /* +0x28 0 loop .. 1000 project */
    short filt[2];            /* +0x2A -1000 (LP) .. 1000 (HP): project, loop */
    unsigned char bpm_keep;   /* +0x2E Tools -> Clock BPM Global */
    unsigned char rsv2f;
    unsigned load_seq;        /* +0x30 one per preset load */
    /* written by the audio task */
    unsigned cap_seen, clr_seen, size_seen, xf_seen, load_seen;   /* +0x34 */
    unsigned char state;      /* +0x48 ST_* */
    unsigned char rec;        /* +0x49 the ring records this block */
    unsigned char done;       /* +0x4A the compressor tap ran this block */
    unsigned char playing;    /* +0x4B transport was playing last block */
    short xf_now;             /* +0x4C 0 .. 1000, for the UI */
    unsigned char route[2];   /* +0x4E UI: R_* into FX A, FX B (DJ FX screen) */
    unsigned have;            /* +0x50 H_*: written by pcmStreamer (mx_stream_poll) */
    unsigned drain_at;        /* +0x54 audio blocks seen when the drain started */
    unsigned wblk;            /* +0x58 blocks written (monotonic) */
    unsigned vblk;            /* +0x5C first block with a valid anchor */
    unsigned blk_a;           /* +0x60 block holding A */
    unsigned rec_stop;        /* +0x64 held: stop recording at this block */
    int len;                  /* +0x68 loop length, ticks */
    int pend;                 /* +0x6C asked length, ticks (bigger: waits for the recording) */
    long long tq;             /* +0x70 this block's start, TQ */
    long long w;              /* +0x78 W: where the capture starts playing, ticks */
    long long a;              /* +0x80 A, ticks */
    long long b;              /* +0x88 phase origin: d = (t - B) mod len, ticks */
    float rate;               /* +0x90 TQ per sample */
    float xf, xf_t;           /* +0x94 crossfader, target: 0 loop .. 1 project */
    float gl, gr;             /* +0x9C gains at the end of the last block */
    float fv[2];              /* +0xA4 filter knobs as the coefficients have them */
    float co[2][5];           /* +0xAC b0 b1 b2 a1 a2, per side */
    float z[2][2][2];         /* +0xD4 state per side, channel */
    unsigned char fon[2];     /* +0xF4 filter on, per side */
    unsigned char filt_off[2];/* +0xF6 UI: the filter taken out (its knob kept), per side */
    unsigned blocks;          /* +0xF8 audio blocks seen */
    unsigned short ent[NENT]; /* +0xFC the borrowed cache entries */
    unsigned char want;       /* the audio task wants the ring: a DJ loop or an audio loop */
    unsigned char nent;       /* entries borrowed so far */
    unsigned char ui_busy;    /* the Song screen is reading the ring (waveform): no give-back now */
    unsigned char proj_n;     /* parts in proj_pk so far */
    unsigned char *proj;      /* PROJ_N peaks of the project, one per PROJ_PARTS parts (dj.c's) */
    unsigned proj_head;       /* peaks written (monotonic) */
    float proj_pk;            /* the peak being gathered */
    unsigned *proj_t;         /* the transport tick of each peak (dj.c's) */
    unsigned proj_v;          /* first peak since the transport last went back: ticks rise from it */
    float *fxb;               /* dj.c's FX buffers: loop, silence, tails (L then R, FX_N each) */
    unsigned char fx_due;     /* mx_eq left the DJ FX out this block: the tap runs them */
} MxState;

typedef char mx_fits[(MX_BASE + sizeof(MxState) <= MX_END) ? 1 : -1];
typedef char mx_after_dj[(MX_BASE >= 0x38007A58u) ? 1 : -1];
typedef char mx_state_at[(__builtin_offsetof(MxState, state) == 0x48) ? 1 : -1];   /* tools/bench */
typedef char mx_have_at[(__builtin_offsetof(MxState, have) == 0x50) ? 1 : -1];
typedef char mx_len_at[(__builtin_offsetof(MxState, len) == 0x68) ? 1 : -1];
typedef char mx_w_at[(__builtin_offsetof(MxState, w) == 0x78) ? 1 : -1];
typedef char mx_ent_at[(__builtin_offsetof(MxState, ent) == 0xFC) ? 1 : -1];
typedef char mx_want_at[(__builtin_offsetof(MxState, want) == 0x202) ? 1 : -1];

#define M ((MxState *)MX_BASE)

typedef unsigned (*pick_fn)(void *);
typedef void (*ramp_fn)(void *, float *, float *, int);
typedef void (*bus_fn)(void *, float **, float **);
typedef int (*len_fn)(void *);
typedef float (*hz_fn)(float);
typedef int (*get_fn)(void *, unsigned);
typedef void (*set_fn)(void *, unsigned, int);
typedef void (*send_fn)(void *, unsigned, unsigned, int, int, int);
typedef void (*post_fn)(void *);
#define CACHE_PICK  ((pick_fn)0x08071D65u)    /* (mgr): a free cache entry, else the least recent */
#define COMP_RAMP   ((ramp_fn)0x0804BE41u)    /* (ramp, L, R, n): the compressor's gain */
#define BUS_LR      ((bus_fn)0x0804D9C1u)     /* (bus, &L, &R) */
#define BUS_LEN     ((len_fn)0x0804D8E9u)     /* (bus): frames in the block */
#define CUT_HZ      ((hz_fn)0x08060641u)      /* stock filter knob 0..1 -> Hz (pad Filter) */
#define PARAM_GET   ((get_fn)0x08093E9Du)
#define PARAM_SET   ((set_fn)0x08093ECDu)
#define ENG_SEND    ((send_fn)0x0804C59Du)    /* (engine, key, id, value, 0, 0): msg 0x39 */
#define LOAD_MSG    ((post_fn)0x0804C765u)    /* SessionMgr_LoadBank: msg 0x36 to the engine */
#define ENGINE      0x2400A9C0u
#define SE          (*(unsigned char *volatile *)(ENGINE + 0x9A74))
#define SE_TEMPO    0x18                      /* float BPM */
#define SE_TICKS    0x30                      /* u64 transport ticks (audio task) */
#define SE_PLAY     0x38
#define GLOBAL_BAG  ((void *)0x24028D18u)     /* App+0x8C90 */
#define SESSION_BAG ((void *)0x240230C0u)     /* App+0x3038: BPM 0x84, Swing 0xD7 */
#define ID_TEMPO    0x84

/* Song DJ state the capture reads (dj.c DjState) */
#define DJ_LOOP_SEQ   (*(volatile unsigned *)0x3800741Cu)
#define DJ_ARMED_LOOP (*(volatile unsigned *)0x38007438u)
#define DJ_LOOP_FROM  (*(volatile long long *)0x38007A50u)
#define DJ_LOOP_LEN   (*(volatile int *)0x3800743Cu)

static void dmb(void) { __asm__ volatile("dmb" ::: "memory"); }
static float fabs_(float x) { return x < 0 ? -x : x; }
static int live(void) { return M->magic == MX_MAGIC; }

typedef long long (*ldiv_fn)(long long, long long);
#define LDIV        ((ldiv_fn)0x080C8495u)    /* stock __aeabi_ldivmod (quotient, toward zero) */

/* No libgcc in bank 1: 64-bit division through stock, float <-> 64-bit through int. */
static long long floor_div(long long a, long long b)
{
    long long q = LDIV(a, b);
    return q * b > a ? q - 1 : q;
}

static long long rate_n(int n) { return (long long)(int)(M->rate * (float)n); }

/* Boot (dj_init): nothing carries over a restart, and no cache entry is borrowed yet. */
void mx_init(void)
{
    unsigned *w = (unsigned *)MX_BASE, i;
    for (i = 0; i < sizeof(MxState) / 4; i++)
        w[i] = 0;
    M->xf = M->xf_t = 1.0f;
    M->xf_now = 1000;
    M->xf_ui = 1000;
    M->gr = 1.0f;
    M->route[0] = M->route[1] = R_PROJ;   /* stock's sound: the project through both */
    M->magic = MX_MAGIC;
}

/* ---- the borrowed ring (pcmStreamer task) ---- */

static unsigned char *entry(unsigned i) { return (unsigned char *)(CACHE + i * 0x1C); }

/* Half h (0 = L, 1 = R) of borrowed entry k: 32 KB. */
static void *half(unsigned k, unsigned h)
{
    return *(void **)(entry(M->ent[k]) + (h ? C_RBUF : C_LBUF));
}

static void give_back(unsigned k)
{
    unsigned char *e = entry(M->ent[k]);
    *(unsigned *)(e + C_BLOCK) = 0;
    *(unsigned *)(e + C_STAMP) = 0;
    *(unsigned *)(e + C_FRAMES) = 0;
    *(unsigned *)(e + C_WAVE) = 0xFFFF;
    *(unsigned short *)(e + C_NEXT) = 0xFFFF;
    e[C_PAIR] = 0;
    dmb();
    e[C_STATE] = 0;
}

/* pcmStreamer's loop (b2load_thunk): borrow the ring when the audio task wants it, give it back
 * when it no longer does and has stopped touching it. Stock's cache is pcmStreamer's own, so this
 * is the only task that changes its entries. */
void mx_stream_poll(void)
{
    if (!live())
        return;
    if (M->have == H_NONE && M->want) {
        while (M->nent < NENT) {
            unsigned i = CACHE_PICK((void *)CACHE) & 0xFFFF, k;
            unsigned char *e = entry(i);
            if (i >= CACHE_N)
                break;
            for (k = 0; k < M->nent && M->ent[k] != i; k++)
                ;
            if (k < M->nent) {               /* nothing evictable: it fell back on one of ours */
                e[C_STATE] = 3;
                break;                       /* try again next time */
            }
            if (!*(void **)(e + C_LBUF) || !*(void **)(e + C_RBUF))
                break;
            e[C_STATE] = 3;                  /* reserved: eviction and recording pass it by */
            *(unsigned *)(e + C_WAVE) = 0xFFFF;
            *(unsigned *)(e + C_BLOCK) = 0;
            *(unsigned *)(e + C_FRAMES) = 0;
            *(unsigned short *)(e + C_NEXT) = 0xFFFF;
            e[C_PAIR] = 0;
            M->ent[M->nent++] = (unsigned short)i;
        }
        if (M->nent == NENT) {
            M->vblk = M->wblk;               /* the ring's timeline starts now */
            dmb();
            M->have = H_READY;
        }
        return;
    }
    if (M->have == H_READY && !M->want) {
        M->drain_at = M->blocks;
        dmb();
        M->have = H_DRAIN;
        return;
    }
    if (M->have == H_DRAIN) {
        if (M->want) {
            M->vblk = M->wblk;
            dmb();
            M->have = H_READY;
            return;
        }
        if (M->blocks - M->drain_at < 2 || M->ui_busy)
            return;                          /* the audio task or the waveform may still read it */
        while (M->nent)
            give_back(--M->nent);
        dmb();
        M->have = H_NONE;
    }
}

static int ready(void) { return M->have == H_READY; }

/* ---- the global BPM (UI task) ---- */

/* SessionMgr_FinalizeBankLoading 0x0809CE82: a loaded preset's BPM (0x84) and Swing go to the
 * engine's tempo node. With BPM Global, or an audio loop held, the BPM stays: the preset's value
 * is replaced in the session bag by the one playing, so the screen and the next save show it. */
void mx_bpm_send(void *eng, unsigned key, unsigned id, int value, int a4, int a5)
{
    if (id == ID_TEMPO && live() && (M->bpm_keep || M->state != ST_IDLE)) {
        unsigned char *se = SE;
        if (se) {
            float bpm = *(float *)(se + SE_TEMPO);
            if (bpm >= 20.0f && bpm <= 400.0f) {
                PARAM_SET(SESSION_BAG, ID_TEMPO, (int)(bpm + 0.5f));
                return;
            }
        }
    }
    ENG_SEND(eng, key, id, value, a4, a5);
}

typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
#define PARAM_ENUM  ((reg_enum_fn)0x0808C0D9u)   /* reg, id, title, labels, count, xml */
#define PARAM_ADD   ((param_add_fn)0x08093EF7u)
static const char t_bpmg[] = "BPM:";
static const char x_bpmg[] = "bpmglobal";
static const char s_project[] = "Project";
static const char s_global[] = "Global";
static const char *const bpmg_names[2] = {s_project, s_global};

/* Param_RegisterAll tail (deck_register): Tools -> Clock "BPM: Project / Global". */
void mx_register(void *reg)
{
    PARAM_ENUM(reg, ID_BPMG, t_bpmg, bpmg_names, 2, x_bpmg);
}

/* Global bag template, last add (seqrec_global_tpl): default Project. Saved in settings.tml. */
void mx_global_tpl(void *bag)
{
    PARAM_ADD(bag, ID_BPMG, 0);
}

/* SessionMgr_LoadBank 0x0809E258 (msg 0x36, first thing a load does): the held loop stops
 * recording, so it never takes in the next project. Also reads BPM Global for the finalize. */
void mx_load_begin(void *eng)
{
    if (live()) {
        M->bpm_keep = PARAM_GET(GLOBAL_BAG, ID_BPMG) != 0;
        dmb();
        M->load_seq++;
    }
    LOAD_MSG(eng);
}

/* ---- the ring (audio task) ---- */

static long long *anc_at(unsigned blk)
{
    blk &= RING_MASK;
    return (long long *)half(E_ANC + (blk >> 13), (blk >> 12) & 1) + (blk & (ANC_PER - 1));
}

static long long anc_of(unsigned blk) { return *anc_at(blk); }

/* Ring frame fr: L | R << 16. */
static unsigned *word(unsigned fr)
{
    unsigned c;
    fr &= RING_FRAMES - 1;
    c = fr / CHUNK_FR;
    return (unsigned *)half(c >> 1, c & 1) + (fr & (CHUNK_FR - 1));
}

static float *scratch(unsigned h) { return (float *)half(E_SCRATCH, 0) + h * BLOCK; }

static unsigned char *peak_at(unsigned blk)
{
    return (unsigned char *)half(E_SCRATCH, 0) + PEAKS_OFF + (blk & RING_MASK);
}

static unsigned char peak_byte(float p)
{
    return (unsigned char)(p >= 1.0f ? 255 : (int)(p * 255.0f));
}

/* The ring block whose anchor is at or before rq, in [lo, hi] (hi = last written). */
static unsigned blk_of(long long rq, unsigned lo, unsigned hi)
{
    long long a0 = anc_of(lo), tpb = rate_n(BLOCK);
    unsigned b = lo;
    if (tpb < 1)
        tpb = 1;
    if (rq > a0) {
        long long est = LDIV(rq - a0, tpb);
        b = est > (long long)(hi - lo) ? hi : lo + (unsigned)est;
    }
    while (b < hi && anc_of(b + 1) <= rq)
        b++;
    while (b > lo && anc_of(b) > rq)
        b--;
    return b;
}

/* Frame position of tick rq (TQ) in the held window: ring block and frames into it. */
static void pos_of(long long rq, unsigned *blk, float *off)
{
    unsigned last = M->wblk - 1, b = blk_of(rq, M->blk_a, last);
    long long a0 = anc_of(b), a1 = b < last ? anc_of(b + 1) : a0 + rate_n(BLOCK);
    float f = a1 > a0 ? (float)(int)(rq - a0) / (float)(int)(a1 - a0) : 0.0f;
    if (f < 0)
        f = 0;
    *blk = b;
    *off = f * BLOCK;
}

static void ring_put(const float *l, const float *r, int n)
{
    unsigned *o = word((M->wblk & RING_MASK) * BLOCK);   /* a block never spans two halves */
    float pk = 0;
    int i;
    for (i = 0; i < n && i < BLOCK; i++) {
        float a = l[i] * 32767.0f, c = r[i] * 32767.0f;
        float al = l[i] < 0 ? -l[i] : l[i], ar = r[i] < 0 ? -r[i] : r[i];
        pk = al > pk ? al : pk;
        pk = ar > pk ? ar : pk;
        int x = (int)(a > 32767.0f ? 32767.0f : a < -32768.0f ? -32768.0f : a);
        int y = (int)(c > 32767.0f ? 32767.0f : c < -32768.0f ? -32768.0f : c);
        o[i] = (unsigned)(x & 0xFFFF) | ((unsigned)y << 16);
    }
    for (; i < BLOCK; i++)
        o[i] = 0;
    *anc_at(M->wblk) = M->tq;
    *peak_at(M->wblk) = peak_byte(pk);
    dmb();
    M->wblk++;
}

static void frame(unsigned fr, float t, float *l, float *r)
{
    unsigned w0 = *word(fr), w1 = *word(fr + 1);
    float l0 = (short)(w0 & 0xFFFF), l1 = (short)(w1 & 0xFFFF);
    float r0 = (short)(w0 >> 16), r1 = (short)(w1 >> 16);
    *l = (l0 + (l1 - l0) * t) * (1.0f / 32768.0f);
    *r = (r0 + (r1 - r0) * t) * (1.0f / 32768.0f);
}

/* n samples of the window from tick rq (TQ), added into lp/rp at weight w0 + k * dw (held in 0..1).
 * No arrays on the audio task's stack. */
static void read_span(long long rq, int n, float *lp, float *rp, float w0, float dw)
{
    unsigned b0, b1;
    float o0, o1, span, step, pos;
    int i;
    pos_of(rq, &b0, &o0);
    pos_of(rq + rate_n(n), &b1, &o1);
    span = (float)(int)(b1 - b0) * BLOCK + o1 - o0;
    step = n ? span / n : 0;
    if (step < 0)
        step = 0;
    pos = o0;
    for (i = 0; i < n; i++, pos += step) {
        int ip = (int)pos;
        float l, r;
        float g = w0 + dw * i;
        g = g < 0 ? 0 : g > 1.0f ? 1.0f : g;
        frame(b0 * BLOCK + (unsigned)ip, pos - ip, &l, &r);
        lp[i] += l * g;
        rp[i] += r * g;
    }
}

/* The loop's audio for this block into the scratch entry (n <= BLOCK). Across the seam the end of the
 * window runs on into what was recorded after it, faded out over SEAM samples. */
static void loop_block(int n)
{
    long long len = (long long)M->len << TQ_SHIFT, rate = rate_n(1);
    long long d = M->tq - (M->b << TQ_SHIFT), base = M->a << TQ_SHIFT;
    long long tail_end = anc_of(M->wblk - 1);
    float *lp = scratch(0), *rp = scratch(1);
    int i = 0;
    d -= floor_div(d, len) * len;
    for (i = 0; i < n; i++)
        lp[i] = rp[i] = 0;
    i = 0;
    while (i < n) {
        int left = rate > 0 ? (int)LDIV(len - d + rate - 1, rate) : n - i;
        int seg = left < n - i ? left : n - i;
        long long since = rate > 0 ? LDIV(d, rate) : SEAM;   /* samples since the seam */
        if (seg < 1)
            seg = 1;
        if (since < SEAM && base + len + d + rate * seg <= tail_end) {
            float w = (float)(int)since / SEAM;
            read_span(base + d, seg, lp + i, rp + i, w, 1.0f / SEAM);
            read_span(base + len + d, seg, lp + i, rp + i, 1.0f - w, -1.0f / SEAM);
        } else {
            read_span(base + d, seg, lp + i, rp + i, 1.0f, 0);
        }
        i += seg;
        d += rate * seg;
        if (d >= len)
            d -= len;
    }
}

/* ---- filters ---- */

static void sincos_(float w, float *s, float *c)
{
    /* w in [0, pi]: x = w - pi/2 in [-pi/2, pi/2], sin w = cos x, cos w = -sin x */
    float x = w - 1.5707963f, x2 = x * x;
    float sx = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
    float cx = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56)));
    *s = cx;
    *c = -sx;
}

/* Bipolar knob v (-1000..1000) as the pad Filter: below 0 low-pass, above 0 high-pass, the cutoff
 * from stock's knob curve (FUN_08060640, the pads' own). RBJ biquad, fixed Q. */
static void design(unsigned side, float v)
{
    float *c = M->co[side], f, s, cs, al, a0, b0, b1;
    int lp = v < 0;
    M->fon[side] = fabs_(v) > DEAD;
    if (!M->fon[side])
        return;
    f = CUT_HZ(lp ? 1.0f + v / 1000.0f : v / 1000.0f);
    if (!(f >= 20.0f))
        f = 20.0f;
    if (f > 20000.0f)
        f = 20000.0f;
    sincos_(6.2831853f * f / 48000.0f, &s, &cs);
    al = s / (2 * FILT_Q);
    a0 = 1.0f / (1 + al);
    if (lp) {
        b1 = (1 - cs);
        b0 = b1 * 0.5f;
    } else {
        b1 = -(1 + cs);
        b0 = -b1 * 0.5f;
    }
    c[0] = b0 * a0;
    c[1] = b1 * a0;
    c[2] = b0 * a0;
    c[3] = -2 * cs * a0;
    c[4] = (1 - al) * a0;
}

static void filter(unsigned side, float *x, unsigned ch, int n)
{
    const float *c = M->co[side];
    float *z = M->z[side][ch], z0 = z[0], z1 = z[1];
    int i;
    for (i = 0; i < n; i++) {
        float in = x[i], y = c[0] * in + z0;
        z0 = c[1] * in - c[3] * y + z1;
        z1 = c[2] * in - c[4] * y;
        x[i] = y;
    }
    z[0] = z0;
    z[1] = z1;
}

static void filters_update(void)
{
    unsigned s;
    for (s = 0; s < 2; s++) {
        float t = M->filt_off[s] ? 0 : M->filt[s], v = M->fv[s] + (t - M->fv[s]) * 0.3f;
        if (fabs_(t - v) < 1.0f)
            v = t;
        if (v != M->fv[s] || (fabs_(v) > DEAD) != M->fon[s]) {
            unsigned was = M->fon[s];
            M->fv[s] = v;
            design(s, v);
            if (!was && M->fon[s])
                M->z[s][0][0] = M->z[s][0][1] = M->z[s][1][0] = M->z[s][1][1] = 0;
        }
    }
}

/* ---- the block ---- */

static void requests(long long t)
{
    if (M->load_seen != M->load_seq) {
        M->load_seen = M->load_seq;
        if (M->state != ST_IDLE)
            M->rec_stop = M->wblk;           /* the next project is not recorded */
    }
    if (M->clr_seen != M->clr_seq) {
        M->clr_seen = M->clr_seq;
        if (M->state != ST_IDLE) {
            M->state = ST_IDLE;
            M->xf_t = 1.0f;
            M->vblk = M->wblk;               /* records afresh after the held window */
        }
    }
    if (M->xf_seen != M->xf_seq) {
        M->xf_seen = M->xf_seq;
        if (M->state == ST_HELD)
            M->xf_t = M->xf_ui * 0.001f;
    }
    if (M->cap_seen != M->cap_seq) {
        long long f, k, len = M->cap_len;
        M->cap_seen = M->cap_seq;
        if (M->state == ST_IDLE && len > 0 && ready() && M->playing) {
            /* the DJ loop's first end at or after it was switched off */
            f = DJ_ARMED_LOOP == M->cap_off ? DJ_LOOP_FROM : t;
            k = floor_div(f - M->cap_a + len - 1, len);
            if (k < 1)
                k = 1;
            M->w = M->cap_a + k * len;
            M->a = M->w - len;
            M->b = M->a;
            M->len = M->pend = (int)len;
            M->state = ST_ARMED;
        }
    }
    if (M->size_seen != M->size_seq) {
        M->size_seen = M->size_seq;
        if (M->state != ST_IDLE && M->size_req > 0)
            M->pend = M->size_req > MAX_TICKS ? MAX_TICKS : M->size_req;
    }
}

/* A new length, keeping where the loop is (as Mixxx: a smaller one goes back in whole sizes). */
static void resize(long long t, int len)
{
    long long d = t - M->b;
    d -= floor_div(d, M->len) * M->len;
    d -= floor_div(d, len) * len;
    M->b = t - d;
    M->len = len;
}

static void held(long long t)
{
    long long rec_end;
    if (M->state == ST_ARMED && t >= M->w) {
        if ((long long)(M->wblk - M->vblk) < 2 || anc_of(M->vblk) > (M->a << TQ_SHIFT)) {
            M->state = ST_IDLE;              /* not in the ring (any more) */
            return;
        }
        M->blk_a = blk_of(M->a << TQ_SHIFT, M->vblk, M->wblk - 1);
        M->rec_stop = M->blk_a + RING_BLOCKS - 1;
        M->state = ST_HELD;
        M->xf_t = 0;                         /* loop only: it is what the project would play */
        M->xf_ui = 0;
    }
    if (M->state != ST_HELD)
        return;
    rec_end = (anc_of(M->wblk - 1) >> TQ_SHIFT) - M->a;
    if (M->pend != M->len && (M->pend < M->len || M->pend <= rec_end))
        resize(t, M->pend);
}

/* The crossfader's gains for this block: from the last block's to the target's, ramped over it.
 * Each side stays at full level up to the middle and only then fades out (a cos quarter), so both are
 * full in the middle. The last XF_DEAD of travel at each end has the other side fully down. */
static void gains(float *l0, float *r0, float *l1, float *r1)
{
    float x1 = M->xf_t;
    *l0 = M->gl;
    *r0 = M->gr;
    if (M->state != ST_HELD) {
        *l1 = 0;
        *r1 = 1.0f;
    } else {
        float x = (x1 - XF_DEAD) / (1.0f - 2 * XF_DEAD), s, c;
        x = x < 0 ? 0 : x > 1.0f ? 1.0f : x;
        if (x <= 0.5f) {                     /* left half: the loop full, the project comes in */
            sincos_(x * 3.1415927f, &s, &c);
            *l1 = 1.0f;
            *r1 = x <= 0 ? 0 : s;
        } else {                             /* right half: the project full, the loop goes */
            sincos_((x - 0.5f) * 3.1415927f, &s, &c);
            *l1 = x >= 1.0f ? 0 : c;
            *r1 = 1.0f;
        }
    }
    M->xf = x1;
    M->gl = *l1;
    M->gr = *r1;
    M->xf_now = (short)(x1 * 1000.0f + 0.5f);
}

/* 128 frames of a master block, in place. The first part of a block reads the engine's transport;
 * a later part (cont) runs on from the first. With ll / lr the loop goes there (zeroed by the caller)
 * instead of into l / r, for the FX. */
static void part(float *l, float *r, float *ll, float *lr, int n, int cont)
{
    unsigned char *se = SE;
    long long t, tq;
    float l0, r0, l1, r1, dl, dr;
    int i, play, loop;
    M->blocks++;
    t = cont ? (M->tq + rate_n(n)) >> TQ_SHIFT : *(volatile long long *)(se + SE_TICKS);
    play = *(volatile unsigned char *)(se + SE_PLAY) != 0;
    {
        float bpm = *(volatile float *)(se + SE_TEMPO);
        if (bpm >= 20.0f && bpm <= 400.0f)
            M->rate = bpm * (960.0f * 65536.0f / 60.0f / 48000.0f);
    }
    tq = M->tq + rate_n(n);   /* own fractional tick, held to the engine's */
    if (!M->playing || tq - (t << TQ_SHIFT) > RESYNC_TQ || (t << TQ_SHIFT) - tq > RESYNC_TQ)
        tq = t << TQ_SHIFT;
    if (play && M->playing && tq < M->tq)
        tq = M->tq;                          /* never backwards while playing */
    if (play && M->playing && (t << TQ_SHIFT) < M->tq - RESYNC_TQ) {
        tq = t << TQ_SHIFT;                  /* the transport restarted: the ring's timeline ends */
        M->vblk = M->wblk;
    }
    M->tq = tq;
    M->playing = (unsigned char)play;
    requests(t);
    M->want = DJ_LOOP_LEN > 0 || M->state != ST_IDLE;   /* mx_stream_poll borrows / gives back */
    if (play && ready())                     /* no ring (not borrowed): never anything to play */
        held(t);

    /* record the project, before anything of ours */
    M->rec = 0;
    if (ready() && play) {
        if (M->state == ST_IDLE)
            M->rec = 1;
        else if (M->wblk < M->rec_stop || M->state == ST_ARMED)
            M->rec = (long long)(anc_of(M->wblk - 1) >> TQ_SHIFT) - M->a < MAX_TICKS + 3840;
    }
    if (M->rec)
        ring_put(l, r, n);
    if (M->proj) {                           /* the project's level for the waveform, before our mix */
        float pk = M->proj_pk;
        for (i = 0; i < n; i++) {
            float al = l[i] < 0 ? -l[i] : l[i], ar = r[i] < 0 ? -r[i] : r[i];
            pk = al > pk ? al : pk;
            pk = ar > pk ? ar : pk;
        }
        if (++M->proj_n >= PROJ_PARTS) {
            unsigned tk = (unsigned)(M->tq >> TQ_SHIFT), k = M->proj_head % PROJ_N;
            if (M->proj_head && tk < M->proj_t[(M->proj_head - 1) % PROJ_N])
                M->proj_v = M->proj_head;    /* the transport went back: a new timeline */
            M->proj[k] = peak_byte(pk);
            M->proj_t[k] = tk;
            dmb();
            M->proj_head++;
            M->proj_n = 0;
            pk = 0;
        }
        M->proj_pk = pk;
    }
    if (M->state == ST_IDLE && M->wblk - M->vblk > RING_BLOCKS)
        M->vblk = M->wblk - RING_BLOCKS;

    filters_update();
    loop = M->state == ST_HELD && play && M->len > 0;
    if (loop)
        loop_block(n);
    gains(&l0, &r0, &l1, &r1);
    if (!loop && !M->fon[0] && r0 == 1.0f && r1 == 1.0f)
        return;                              /* nothing of ours this block */
    if (M->fon[0]) {
        filter(0, l, 0, n);
        filter(0, r, 1, n);
    }
    if (loop && M->fon[1]) {
        filter(1, scratch(0), 0, n);
        filter(1, scratch(1), 1, n);
    }
    dl = (l1 - l0) / n;
    dr = (r1 - r0) / n;
    {
        const float *lp = loop ? scratch(0) : 0, *rp = loop ? scratch(1) : 0;
        for (i = 0; i < n; i++) {
            float gl = l0 + dl * (i + 1), gr = r0 + dr * (i + 1);
            l[i] *= gr;
            r[i] *= gr;
            if (!loop)
                continue;
            if (ll) {
                ll[i] = lp[i] * gl;
                lr[i] = rp[i] * gl;
            } else {
                l[i] += lp[i] * gl;
                r[i] += rp[i] * gl;
            }
        }
    }
}

/* ---- the DJ FX, behind the compressor (audio task) ---- */

typedef int (*proc_fn)(void *, void *);

/* A node's process (vt +0x0C): the EQ node, the FX container, an FX slot. */
static void proc(void *obj, void *se) { ((proc_fn)(*(void *const *const *)obj)[3])(obj, se); }

/* 0x08053522 (mx_eq_thunk) in the master render: the master EQ node, which ends by processing its
 * next stage, the DJ FX container. That is left out here; our tap runs the FX after the compressor. */
void mx_eq(void *node, unsigned char *se)
{
    unsigned *nd = (unsigned *)node, fx = nd[2];
    if (!live() || !M->fxb || fx != (unsigned)(se + SE_FX)) {
        proc(node, se);
        return;
    }
    nd[2] = 0;
    proc(node, se);
    nd[2] = fx;
    M->fx_due = 1;
}

static void zero(float *d, int n)
{
    int i;
    for (i = 0; i < n; i++)
        d[i] = 0;
}

static void add(float *d, const float *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        d[i] += s[i];
}

/* One FX slot on l / r: an FX reads and writes bus 0 in place (FUN_0805F37C(SE, fx +0x1E = 0)), so
 * bus 0 points at l / r around it and is put back after. */
static void slot_on(unsigned char *se, void *slot, float *l, float *r, int n)
{
    unsigned *bus = (unsigned *)(se + SE_BUS0), keep[5], i;
    float *a = 0, *b = 0;
    for (i = 0; i < 5; i++)
        keep[i] = bus[i];
    bus[1] = (unsigned)n;
    bus[2] = (unsigned)l;
    bus[3] = (unsigned)r;
    ((unsigned char *)bus)[0x10] = 0;        /* not silent, */
    ((unsigned char *)bus)[0x11] = 1;        /* both channels */
    proc(slot, se);
    BUS_LR(bus, &a, &b);                     /* silent or mono after it: zeroed / L copied to R */
    for (i = 0; a != l && a && i < (unsigned)n; i++)
        l[i] = a[i];
    for (i = 0; b != r && b && i < (unsigned)n; i++)
        r[i] = b[i];
    for (i = 0; i < 5; i++)
        bus[i] = keep[i];
}

/* FX A then FX B on P (l / r) and L (ll / lr). A slot takes the sum of what is routed to it; what
 * went through an FX together stays mixed for the next one, and what isn't routed passes by it. A
 * slot with nothing routed runs on silence, so its tails ring on into the output. Further slots
 * (stock has 2) take the project as stock. Then the container's own next stage, as stock. */
static void fx_run(unsigned char *se, float *l, float *r, float *ll, float *lr, int n)
{
    unsigned char *c = se + SE_FX;
    float *sl = lr + FX_N, *sr = sl + FX_N, *tl = sr + FX_N, *tr = tl + FX_N;
    unsigned k, cnt = *(unsigned short *)(c + FXC_COUNT);
    int merged = 0, tails = 0;
    void *next;
    for (k = 0; !c[FXC_BYPASS] && k < cnt; k++) {
        void *slot = c + FXC_SLOTS + k * FXC_SLOT_SZ;
        unsigned rt = k < 2 ? M->route[k] & (R_PROJ | R_LOOP) : R_PROJ;
        if (!rt) {
            zero(sl, n);
            zero(sr, n);
            slot_on(se, slot, sl, sr, n);
            if (!tails) {
                zero(tl, n);
                zero(tr, n);
                tails = 1;
            }
            add(tl, sl, n);
            add(tr, sr, n);
        } else if (merged || rt == R_PROJ) {
            slot_on(se, slot, l, r, n);
        } else if (rt == R_LOOP) {
            slot_on(se, slot, ll, lr, n);
        } else {
            add(l, ll, n);
            add(r, lr, n);
            merged = 1;
            slot_on(se, slot, l, r, n);
        }
    }
    if (!merged) {
        add(l, ll, n);
        add(r, lr, n);
    }
    if (tails) {
        add(l, tl, n);
        add(r, tr, n);
    }
    next = *(void **)(c + 8);
    if (next)
        proc(next, se);
}

/* One master block, in place: the audio task's blocks are 256 frames, the ring's 128. */
void mx_block(float *l, float *r, int n)
{
    unsigned char *se = SE;
    float *ll = 0, *lr = 0;
    int k, fx;
    if (!live())
        return;
    fx = M->fx_due;
    M->fx_due = 0;
    if (!se || n <= 0 || n > 8 * BLOCK) {
        if (fx && se)
            proc(se + SE_FX, se);            /* the FX as stock, on bus 0 */
        return;
    }
    if (fx && n <= FX_N) {
        ll = M->fxb;
        lr = ll + FX_N;
        zero(ll, n);
        zero(lr, n);
    }
    for (k = 0; k < n; k += BLOCK)
        part(l + k, r + k, ll ? ll + k : 0, lr ? lr + k : 0, n - k < BLOCK ? n - k : BLOCK, k > 0);
    if (ll)
        fx_run(se, l, r, ll, lr, n);
    else if (fx)
        proc(se + SE_FX, se);
}

/* 0x0804EE9A in FUN_0804EDDC (compressor on): the compressor's gain, then ours, before the limiter. */
void mx_comp_tap(void *ramp, float *l, float *r, int n)
{
    COMP_RAMP(ramp, l, r, n);
    mx_block(l, r, n);
    if (live())
        M->done = 1;
}

/* 0x0805354A in the master render, where both paths meet (bus 0's L / R for the output volumes).
 * With the compressor off this is our only tap: no limiter follows, as stock. */
void mx_bus_tap(void *bus, float **l, float **r)
{
    BUS_LR(bus, l, r);
    if (!live())
        return;
    if (M->done) {
        M->done = 0;
        return;
    }
    if (*l && *r)
        mx_block(*l, *r, BUS_LEN(bus));
}

/* ---- for the Song screen (UI task) ---- */

int mx_state(void) { return live() ? M->state : 0; }
int mx_len(void) { return live() ? M->len : 0; }
int mx_xf(void) { return live() ? M->xf_now : 1000; }
/* The crossfader's gains now, per mille (for the strip's level bars). */
void mx_gains(int *l, int *r)
{
    *l = live() ? (int)(M->gl * 1000.0f + 0.5f) : 0;
    *r = live() ? (int)(M->gr * 1000.0f + 0.5f) : 1000;
}

int mx_xf_target(void) { return live() ? M->xf_ui : 1000; }   /* where the UI last put it */
int mx_filter(unsigned side) { return live() && side < 2 ? M->filt[side] : 0; }
/* The ring is borrowed and in use (for the strip). */
int mx_have(void) { return live() && ready(); }

/* BACK may capture the DJ loop (a, len): the ring has recorded the whole pass playing now. */
int mx_ready(long long a, int len)
{
    long long now, pass;
    if (!live() || !ready() || len <= 0 || M->wblk - M->vblk < 2)
        return 0;
    now = M->tq >> TQ_SHIFT;
    pass = now < a ? a : a + floor_div(now - a, len) * len;
    return (anc_of(M->vblk) >> TQ_SHIFT) <= pass;
}

/* BACK with the DJ loop on: the DJ loop (a, len) was switched off by loop_seq off_seq. */
void mx_capture(long long a, int len, unsigned off_seq)
{
    if (!live())
        return;
    M->cap_a = a;
    M->cap_len = len;
    M->cap_off = off_seq;
    dmb();
    M->cap_seq++;
}

void mx_clear(void)
{
    if (!live())
        return;
    M->clr_seq++;
    M->xf_ui = 1000;
}

void mx_size(int ticks)
{
    if (!live())
        return;
    M->size_req = ticks;
    dmb();
    M->size_seq++;
}

void mx_set_xf(int v)
{
    if (!live())
        return;
    M->xf_ui = (short)(v < 0 ? 0 : v > 1000 ? 1000 : v);
    dmb();
    M->xf_seq++;
}

int mx_filter_on(unsigned side) { return live() && side < 2 ? !M->filt_off[side] : 1; }

/* Loop / Proj on the deck: the filter in or out; out glides to off, the knob's place is kept. */
void mx_set_filter_on(unsigned side, int on)
{
    if (live() && side < 2)
        M->filt_off[side] = (unsigned char)!on;
}

void mx_set_filter(unsigned side, int v)
{
    if (live() && side < 2)
        M->filt[side] = (short)(v < -1000 ? -1000 : v > 1000 ? 1000 : v);
}

/* DJ FX screen: what goes into FX A (slot 0) / FX B (1), R_PROJ | R_LOOP. */
int mx_route(unsigned slot) { return live() && slot < 2 ? M->route[slot] : R_PROJ; }

void mx_set_route(unsigned slot, int r)
{
    if (live() && slot < 2)
        M->route[slot] = (unsigned char)(r & (R_PROJ | R_LOOP));
}

/* dj.c's FX buffers: 6 x FX_N floats, kept for good. */
void mx_set_fx(float *buf)
{
    if (live())
        M->fxb = buf;
}

/* ---- the waveform (UI task) ---- */

/* dj.c's buffers for the project's peaks and their ticks (PROJ_N each, kept for good). */
void mx_set_proj(unsigned char *buf, unsigned *ticks)
{
    if (live()) {
        M->proj_t = ticks;
        M->proj = buf;
    }
}

/* The transport tick the audio task is at. */
long long mx_now(void) { return live() ? M->tq >> TQ_SHIFT : 0; }

/* The newest project peak at or before tick tk, of those in [lo, hi); -1 = none. */
static int proj_at(unsigned lo, unsigned hi, long long tk)
{
    if (lo >= hi || tk < (long long)M->proj_t[lo % PROJ_N])
        return -1;
    while (hi - lo > 1) {
        unsigned mid = lo + (hi - lo) / 2;
        if ((long long)M->proj_t[mid % PROJ_N] <= tk)
            lo = mid;
        else
            hi = mid;
    }
    return M->proj[lo % PROJ_N];
}

/* The project's loop (the Loop button's) in the bottom lane, from the project's peaks: n columns over
 * one pass [a + k * len, + len); a column past the playhead shows the pass before. Returns the
 * playhead's column. Works without the ring (which may be holding the audio loop). */
int mx_wave_pass(unsigned char *out, int n, long long a, int len)
{
    long long now, s0, tk;
    unsigned head, lo;
    int i, v;
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (!live() || !M->proj || !M->proj_t || len <= 0 || n <= 0)
        return -1;
    now = M->tq >> TQ_SHIFT;
    s0 = a + floor_div(now - a, len) * len;
    head = M->proj_head;
    lo = head - M->proj_v > PROJ_N - 1 ? head - (PROJ_N - 1) : M->proj_v;   /* one slot spare: */
    for (i = 0; i < n; i++) {                                               /* it is being written */
        tk = s0 + LDIV((long long)len * i, n);
        if (tk > now)
            tk -= len;
        if (tk < a)
            continue;                    /* before the loop started */
        v = proj_at(lo, head, tk);
        if (v > 0)
            out[i] = (unsigned char)v;
    }
    return (int)LDIV((now - s0) * n, len);
}

/* The project's last n peaks (n <= PROJ_N), oldest first, into out. */
void mx_wave_proj(unsigned char *out, int n)
{
    unsigned head;
    int i;
    if (!live() || !M->proj) {
        for (i = 0; i < n; i++)
            out[i] = 0;
        return;
    }
    head = M->proj_head;
    for (i = 0; i < n; i++) {
        unsigned k = head - (unsigned)n + (unsigned)i;
        out[i] = (int)(head - k) <= (int)PROJ_N && k < head ? M->proj[k % PROJ_N] : 0;
    }
}

/* The audio loop's lane: n peaks over its window [A, A + len), once BACK has grabbed it. Returns the
 * playhead's column (-1 = none yet, -2 = no audio loop: then mx_wave_done is not needed) and the
 * window in *start / *len. The ring's give-back waits for mx_wave_done. */
int mx_wave_loop(unsigned char *out, int n, long long *start, int *len)
{
    long long t, s0, rq;
    unsigned lo, hi, b;
    int i, l, head = -1;
    if (!live() || n <= 0)
        return -2;
    M->ui_busy = 1;
    dmb();
    if (!ready() || M->state == ST_IDLE || M->wblk - M->vblk < 2 || M->len <= 0) {
        M->ui_busy = 0;
        return -2;
    }
    t = M->tq >> TQ_SHIFT;
    l = M->len;
    s0 = M->a;
    if (M->state == ST_HELD)
        head = (int)LDIV((long long)(((t - M->b) - floor_div(t - M->b, l) * l)) * n, l);
    lo = M->state == ST_HELD ? M->blk_a : M->vblk;
    hi = M->wblk - 1;
    rq = s0 << TQ_SHIFT;
    b = rq > anc_of(lo) ? blk_of(rq, lo, hi) : lo;   /* then walk on: the columns only go forward */
    for (i = 0; i < n; i++) {
        rq = (s0 + LDIV((long long)l * i, n)) << TQ_SHIFT;
        if (rq < anc_of(lo) || rq > anc_of(hi)) {
            out[i] = 0;
            continue;
        }
        while (b < hi && anc_of(b + 1) <= rq)
            b++;
        out[i] = *peak_at(b);
    }
    *start = s0;
    *len = l;
    return head;
}

void mx_wave_done(void)
{
    dmb();
    if (live())
        M->ui_busy = 0;
}
