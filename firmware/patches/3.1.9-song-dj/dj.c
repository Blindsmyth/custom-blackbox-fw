/* Song DJ row (3.1.T): Beatjump << / >> and Autoloop for every playing Clip, pad sequence and the
 * song, from the Song screen. Design and as built: docs/song-dj.md.
 *
 * SONG on the Song screen shows a row at the bottom: << | >> | Loop. Bottom-left knob: the jump size
 * (1/4 bar .. 64 bars, default 16), bottom-right: the loop size (1/16 .. 16 bars, default 1).
 *
 * One timeline for everything. The UI posts a request (a sequence number and a size in ticks); the
 * first user in the audio task that sees it arms it: a jump is taken on the first beat at least
 * DJ_LEAD ticks after that user's tick (armed_b), a loop is a window [loop_a, loop_a + loop_len) in
 * transport ticks. Each user (a Clip through deck.c deck_wrap_unit, a sequence player through the
 * Player_Tick hooks in dj.S, the song player among them) turns that into its own units or steps:
 *   jump   at its first unit / step starting at or after armed_b, + jump / step (toward zero)
 *   loop   every lp ticks from loop_a (lp = the loop rounded up to whole units / steps), - lp
 * The transport is never moved: only what each user plays. With Song playback on, the song player
 * moves with them, so the scene ends correspondingly earlier or later, and a jump that would leave
 * the scene (its end, or its start going back) is not taken: the scene it would have reached blinks.
 * The loop is cut on the scene's grid and never crosses the scene's end.
 *
 * The mixer (mixer.c) adds the audio loop: BACK with Loop on captures it, and while it plays the Loop
 * button and the loop knob act on it. The top knobs are the project and loop filters while the row is
 * shown, and a crossfader strip sits above the row.
 */

#define DJ_BASE      0x38007400u   /* SRAM4: past RbVoice + diag (0x38007000-0x38007148), below */
#define DJ_END       0x38008000u   /* Bungee's grain input (0x38008000) */
#define DJ_MAGIC     0x314A4453u   /* 'SDJ1' */
#define DJ_PADS      16
#define DJ_PLAYERS   20            /* 16 pad sequences, the song, spare */
#define TICKS_BEAT   960
#define DJ_LEAD      240           /* a jump waits for the first beat at least this far ahead */
#define DJ_STALE_MS  300           /* a jump nobody plays within this long is dropped */
#define JUMP_SIZES   9
#define JUMP_DEF     6             /* 16 bars */
#define LOOP_SIZES   9
#define LOOP_DEF     4             /* 1 bar */
#define ID_DJ        0x1D0         /* our buttons: << >> Loop */
#define NBTN         3
#define STEP_COUNTS  400           /* encoder counts per step, like stock list knobs */
#define BLINK_MS     600
#define BLINK_HALF   100
#define NO_SCENE     0xFF

typedef struct {
    unsigned jump_seen;       /* armed_jump this user took (or let pass) */
    unsigned loop_seen;       /* armed_loop this user follows */
    long long wrap_at;        /* tick of its next loop wrap */
    int lp;                   /* its loop period in ticks (whole units / steps), 0 = not looping */
    int rsv;
} DjUser;                     /* 24 bytes */

typedef struct {
    void *song;               /* +0x00 the Song widget the row was built on, 0 = not yet */
    unsigned char want;       /* +0x04 row asked for (SONG on the Song screen toggles it) */
    unsigned char shown;      /* +0x05 row on screen */
    unsigned char jump_idx;   /* +0x06 JUMP_SIZES index */
    unsigned char loop_idx;   /* +0x07 LOOP_SIZES index */
    unsigned char loop_on;    /* +0x08 */
    unsigned char lit;        /* +0x09 Loop button lit as last set */
    unsigned char lbl_jump;   /* +0x0A jump_idx the labels show, 0xFF = none */
    unsigned char lbl_loop;   /* +0x0B */
    int acc[2];               /* +0x0C encoder counts not yet a step: bottom left, bottom right */
    unsigned refuse_seen;     /* +0x14 refuse_seq last blinked */
    unsigned blink_t0;        /* +0x18 UI tick the blink started */
    unsigned char blink;      /* +0x1C scene blinking, NO_SCENE = none */
    unsigned char led;        /* +0x1D SONG LED as last set while the row is shown: 1 on, 2 off, 0 not ours */
    unsigned char rsv[2];
    char text[NBTN][16];      /* +0x20 labels */
} DjUi;                       /* 0x50 */

typedef struct {
    unsigned magic;           /* +0x00 */
    unsigned save[3];         /* +0x04 hook_dj_step0 (dj.S): lr */
    /* written by the UI task */
    unsigned jump_seq;        /* +0x10 one per << / >> press */
    int jump_ticks;           /* +0x14 signed */
    unsigned jump_ms;         /* +0x18 UI tick of the press */
    unsigned loop_seq;        /* +0x1C one per Loop on / off / size change */
    int loop_ticks;           /* +0x20 0 = off */
    /* written by the audio task */
    unsigned armed_jump;      /* +0x24 jump_seq the next three are for */
    int armed_j;              /* +0x28 ticks; 0 = dropped or refused (scene wall) */
    int loop_delta;           /* +0x2C a resize: where the new loop starts against the old one's start */
    long long armed_b;        /* +0x30 the beat it is taken at */
    unsigned armed_loop;      /* +0x38 loop_seq the loop fields are for */
    int loop_len;             /* +0x3C ticks, 0 = off */
    long long loop_a;         /* +0x40 loop start, transport ticks */
    int loop_s, loop_e;       /* +0x48 the song's loop window in scene ticks (Song playback on) */
    unsigned refuse_seq;      /* +0x50 one per refused jump */
    unsigned char refuse_scene; /* +0x54 the scene the jump would have reached */
    unsigned char song_forced;  /* +0x55 the song player's song flag (+0x60) is off for this step */
    unsigned char loop_resize;  /* +0x56 the armed loop is a new size of the one playing */
    unsigned char rsv57;
    void *song;               /* +0x58 the song player (sequence player with +0x60 set) */
    int song_step;            /* +0x5C step starting at song_tick (may be the scene's count) */
    long long song_tick;      /* +0x60 */
    int song_count;           /* +0x68 the scene's steps (1 beat each), 0 = not known yet */
    unsigned rsv6c;
    void *player[DJ_PLAYERS]; /* +0x70 sequence players seen, for seq[] */
    DjUser clip[DJ_PADS];     /* +0xC0 */
    DjUser seq[DJ_PLAYERS];   /* +0x240 */
    DjUi ui;                  /* +0x420 */
    unsigned btn[NBTN][0x28]; /* +0x470 three stock buttons, 0xA0 each */
    long long loop_from;      /* +0x650 tick the armed loop was armed at: users switch to it from there */
} DjState;

typedef char dj_fits[(DJ_BASE + sizeof(DjState) <= DJ_END) ? 1 : -1];
typedef char dj_save_at[(__builtin_offsetof(DjState, save) == 0x04) ? 1 : -1];          /* DJ_SAVE in dj.S */
typedef char dj_forced_at[(__builtin_offsetof(DjState, song_forced) == 0x55) ? 1 : -1];
typedef char dj_btn_at[(__builtin_offsetof(DjState, btn) == 0x470) ? 1 : -1];

#define D ((DjState *)DJ_BASE)

typedef void (*view_fn)(void *);
typedef void (*rect_fn)(void *, const int *);
typedef void (*text_fn)(void *, const char *);
typedef void (*state_fn)(void *, unsigned);
typedef void (*set_id_fn)(void *, unsigned);
typedef void (*screen_fn)(void *, unsigned, unsigned, unsigned);
typedef void (*tick_fn)(void *, void *, void *, void *);
typedef unsigned (*child_fn)(void *, const void *);
typedef void (*event_fn)(void *, const void *);
typedef long long (*ldiv_fn)(long long, long long);
typedef void (*led_fn)(void *, unsigned, unsigned);

#define BTN_CTOR    ((view_fn)0x080A3D35u)     /* stock frame button (0xA0 bytes) */
#define BTN_RECT    ((rect_fn)0x080A3759u)     /* button vt+0x20: x, y, w, h */
#define BTN_TEXT    ((text_fn)0x080A3DEDu)     /* copies the text */
#define BTN_STATE   ((state_fn)0x080A3E01u)    /* lit */
#define BTN_LATCH   ((state_fn)0x080A3773u)    /* +0x97: draws in the partial pass, as the scene rows */
#define HIDE        ((state_fn)0x080C4E61u)    /* hidden flag (+0x30) + redraw */
#define ROW_LIT     ((state_fn)0x080C2319u)    /* Song scene row: selected look (+0x7A4) + redraw */
#define SET_SCREEN  ((screen_fn)0x0809EAEDu)   /* App_SetScreen */
#define SONG_TICK   ((tick_fn)0x080C26FDu)     /* Song vtable +0x00 */
#ifdef SONG_CUES
/* 3.1.T: Song cues (deck.c) wrap the same two slots; the DJ row sits outside them and hands on to them. */
extern int song_event(void *song, void *ev);
extern void song_event2(void *song, void *ev);
#define SONG_CHILD(s, e)  ((unsigned)song_event((s), (void *)(e)))
#define SONG_EVENT(s, e)  song_event2((s), (void *)(e))
#else
#define SONG_CHILD  ((child_fn)0x080C3541u)    /* Song vtable +0x30 */
#define SONG_EVENT  ((event_fn)0x080C3861u)    /* Song vtable +0x34 */
#endif
#define LDIV        ((ldiv_fn)0x080C8495u)     /* stock __aeabi_ldivmod (quotient, toward zero) */
typedef void *(*alloc_fn)(unsigned);
#define SDRAM_ALLOC ((alloc_fn)0x0804436Du)    /* bump from the bottom of SDRAM, at boot only */
#define UI_TICK     (*(volatile const unsigned *)0x2402EEE0u)   /* osKernelSysTick, ms */
#define LED_SET     ((led_fn)0x080436F9u)      /* (leds, index, on): mode button LED */
#define LEDS        ((void *)0x240021BCu)      /* as App_SetScreen passes it */
#define LED_SONG    3
#define LED_HALF    250        /* SONG blinks while the row is shown */

#define APP_SCREEN  0x8CA4     /* App: screen shown */
#define SCR_SONG    0x2D
#define S_ROWS      0x34       /* Song: 32 scene rows, 0x7F8 each */
#define S_ROW_SIZE  0x7F8
#define S_NROWS     32
#define ROW_SCENE   0x7A0      /* row: scene index */
#define S_BAR_HID   0x10014    /* Song: top button bar hidden (grid modes) */
#define S_SCENE     0x12180    /* Song: selected scene */
#define S_MODE      0x12194    /* Song: byte, 0 list, 1 Edit popup, 2 / 3 grids */
#define W_HIDDEN    0x30
#define S_LIST      0x1067C    /* Song: the scene list (container of the 32 rows), 0..180 */
#define S_BAR       0xFFE4     /* Song: the top button bar, 182..220 (its hidden byte is S_BAR_HID) */
#define BTN_TEXT_AT 0x65       /* button: its label is embedded at +0x34, the text at label +0x31 */
#define SCENE_TAB   0x29C0     /* App: 32 scenes of 0x30; the name (a str) at +0x2C */
#define SCENE_SIZE  0x30
#define SCENE_NAME  0x2C
#define PL_LAYER    0xC20      /* sequence player: playing layer (the song: the scene) */
#define PL_QUEUED   0xC24      /*                  queued layer (a touched scene) */
#define LAY_INDEX   0x40       /* layer: its index (u16) */
#define KNOB_TL     0          /* encoder index in event 0x32 */
#define KNOB_BL     1
#define KNOB_TR     2
#define KNOB_BR     3
#define APP         0x24020088u
#define APP_SCENE   (*(volatile unsigned *)(APP + 0x20))       /* the scene last pressed / queued */
#define SESSION_BAG ((void *)(APP + 0x3038))
#define ID_SCENES   0xC1
#define EV_SCENE    0x107      /* a scene row pressed: the app selects it, or queues it on the bar */
#define EV_TAG      0x080CFBA4u
typedef int (*pget_fn)(void *, unsigned);
typedef void (*post_fn)(void *, void *);
#define PARAM_GET   ((pget_fn)0x08093E9Du)
#define UI_POST_UP  ((post_fn)0x080AED15u)
#define EV_BACK     0x7F       /* BACK on the screen */
#define FILT_STEP   8          /* encoder counts per filter step of 1 (of -1000..1000) */

/* The deck view's widgets, ids after the row's (0x1D0-0x1D2): */
#define XF_ID       0x1D3      /* crossfader */
#define ID_PANEL    0x1DA      /* background */
#define ID_UP       0x1DB      /* scene prev */
#define ID_DN       0x1DC      /* scene next */
#define ID_LINE     0x1DD      /* scene line */
#define ID_WAVE     0x1DE
#define ID_CAP0     0x1DF      /* "Loop" */
#define ID_CAP1     0x1E0      /* "Proj" */
#define ID_SL0      0x1E1      /* loop filter */
#define ID_SL1      0x1E2      /* project filter */
#define N_DECK      9

/* The deck's own widgets and the project's peaks: 2.3 KB from the bottom SDRAM allocator at boot
 * (SRAM4 has no room; SDRAM has ~1 MB free after boot, measured on the unit). */
typedef struct {
    unsigned panel[0x28], up[0x28], dn[0x28], line[0x28], wave[0x28];
    unsigned cap[2][0x28], sl[2][0x28];        /* stock buttons, 0xA0 each */
    unsigned char proj[512];                   /* mixer.c's project peaks (PROJ_N) */
    unsigned char lane[2][160];                /* columns being drawn */
} DjDeck;

/* The crossfader and the deck's state: SRAM4 0x38006F00, free on 3.1.T between Song cues' SgState
 * (ends 0x38006C80) and RbVoice (0x38007000). */
#define XF_BASE     0x38006F00u
#define XF_END      0x38007000u
typedef struct {
    unsigned btn[0x28];        /* +0x00 the crossfader: a stock button with our vtable (xf_vt) */
    int drawn;                 /* +0xA0 crossfader as last drawn */
    DjDeck *deck;              /* +0xA4 0 = no memory: the deck is never shown */
    unsigned wave_ms;          /* +0xA8 UI tick the waveform was last asked to repaint */
    int sl_drawn[2];           /* +0xAC filter sliders as last drawn: loop, project */
    int facc[2];               /* +0xB4 encoder counts not yet a filter step: project, loop */
    unsigned char hid_list;    /* +0xBC stock's scene list was hidden before the deck */
    unsigned char hid_bar;     /* +0xBD stock's top bar likewise */
    unsigned char ask;         /* +0xBE repaint passes our tick asks the GUI for: 1 partial, 2 full */
    unsigned char rsv;
    char line[32];             /* +0xC0 scene line text */
} DjXf;
typedef char xf_fits[(XF_BASE + sizeof(DjXf) <= XF_END) ? 1 : -1];
#define X ((DjXf *)XF_BASE)

void mx_init(void);
int mx_state(void);
int mx_len(void);
int mx_xf(void);
int mx_filter(unsigned side);
int mx_ready(long long a, int len);
int mx_have(void);
void mx_gains(int *l, int *r);
void mx_set_proj(unsigned char *buf);
void mx_wave_proj(unsigned char *out, int n);
int mx_wave_loop(unsigned char *out, int n, int dj_len, long long *start, int *len);
void mx_wave_done(void);
void mx_capture(long long a, int len, unsigned off_seq);
void mx_clear(void);
void mx_size(int ticks);
void mx_set_xf(int v);
void mx_set_filter(unsigned side, int v);
int mx_filter_on(unsigned side);
void mx_set_filter_on(unsigned side, int on);

static unsigned u8(const void *p, unsigned off) { return ((const unsigned char *)p)[off]; }
static unsigned u16(const void *p, unsigned off) { return *(const unsigned short *)((const char *)p + off); }
static unsigned u32(const void *p, unsigned off) { return *(const unsigned *)((const char *)p + off); }
static void *at(void *p, unsigned off) { return (char *)p + off; }
static void dmb(void) { __asm__ volatile("dmb" ::: "memory"); }

static const int jump_t[JUMP_SIZES] = {960, 1920, 3840, 7680, 15360, 30720, 61440, 122880, 245760};
static const int loop_t[LOOP_SIZES] = {240, 480, 960, 1920, 3840, 7680, 15360, 30720, 61440};
static const char n_16[] = "1/16";
static const char n_8[] = "1/8";
static const char n_4[] = "1/4";
static const char n_2[] = "1/2";
static const char n_1[] = "1";
static const char n_2b[] = "2";
static const char n_4b[] = "4";
static const char n_8b[] = "8";
static const char n_16b[] = "16";
static const char n_32b[] = "32";
static const char n_64b[] = "64";
static const char *const jump_n[JUMP_SIZES] = {n_4, n_2, n_1, n_2b, n_4b, n_8b, n_16b, n_32b, n_64b};
static const char *const loop_n[LOOP_SIZES] = {n_16, n_8, n_4, n_2, n_1, n_2b, n_4b, n_8b, n_16b};
static const char s_back[] = "<< ";
static const char s_fwd[] = " >>";
static const char s_loop[] = "Loop ";
static const char s_audio[] = "Audio ";
static const char s_capture[] = "BACK: capture loop";
static const char s_recording[] = "Recording the loop...";
static const char s_loop_first[] = "Loop on, then BACK";
static const char s_no_ring[] = "Loop: getting memory";
static const char s_next[] = "  > ";

static int live(void)
{
    return D->magic == DJ_MAGIC;
}

/* Boot (deck_init): nothing carries over a restart. */
void dj_init(void)
{
    unsigned *w = (unsigned *)DJ_BASE, i;
    for (i = 0; i < sizeof(DjState) / 4; i++)
        w[i] = 0;
    D->ui.jump_idx = JUMP_DEF;
    D->ui.loop_idx = LOOP_DEF;
    D->ui.lbl_jump = 0xFF;
    D->ui.lbl_loop = 0xFF;
    D->ui.blink = NO_SCENE;
    D->refuse_scene = NO_SCENE;
    D->magic = DJ_MAGIC;
    for (i = 0; i < sizeof(DjXf) / 4; i++)
        ((unsigned *)XF_BASE)[i] = 0;
    mx_init();
    {
        DjDeck *d = (DjDeck *)SDRAM_ALLOC(sizeof(DjDeck));
        if ((unsigned)d >= 0xC0000000u && (unsigned)d + sizeof(DjDeck) <= 0xC4000000u) {
            unsigned char *c = (unsigned char *)d;
            for (i = 0; i < sizeof(DjDeck); i++)
                c[i] = 0;
            X->deck = d;
            mx_set_proj(d->proj);
        }
    }
}

/* ---- the timeline (audio task) ---- */

static long long floor_div(long long a, long long b)
{
    long long q = LDIV(a, b);
    return q * b > a ? q - 1 : q;
}

static long long ceil_div(long long a, long long b)
{
    return -floor_div(-a, b);
}

static int song_on(void)
{
    void *p = D->song;
    return p && u8(p, 0x30) && (u8(p, 0x60) || D->song_forced) && D->song_count > 0;
}

/* Scene start, in transport ticks as the song plays now (moved by every jump and loop wrap). */
static long long song_start(void)
{
    return D->song_tick - (long long)D->song_step * TICKS_BEAT;
}

/* The scene a jump out of the current one would reach, as the song moves on (Loop1 stays). */
static unsigned song_neighbour(int dir)
{
    void *p = D->song, *layer = (void *)u32(p, 0xC20);
    int cur = layer ? (int)u16(layer, 0x40) : 0, n = (int)u32(p, 0x64);
    if (!u8(p, 0x5C))
        return (unsigned)cur;
    if (dir > 0)
        return (unsigned)(cur + 1 < n ? cur + 1 : u8(p, 0x5D) ? 0 : cur);
    return (unsigned)(cur > 0 ? cur - 1 : u8(p, 0x5D) && n > 0 ? n - 1 : cur);
}

static void arm_jump(long long t)
{
    unsigned seq = D->jump_seq;
    int j = D->jump_ticks;
    long long b = ceil_div(t + DJ_LEAD, TICKS_BEAT) * TICKS_BEAT;
    if (UI_TICK - D->jump_ms > DJ_STALE_MS)
        j = 0;                           /* nothing played for a while: not taken later */
    if (j && song_on()) {
        long long len = (long long)D->song_count * TICKS_BEAT, p = b - song_start(), lo = p, hi = p + 1;
        if (D->loop_len > 0 && D->loop_e > D->loop_s) {
            lo = D->loop_s;              /* the whole loop moves: it must stay in the scene */
            hi = D->loop_e;
        }
        if (lo + j < 0 || hi + j > len) {
            D->refuse_scene = (unsigned char)song_neighbour(j);
            dmb();
            D->refuse_seq++;
            j = 0;
        } else if (D->loop_len > 0) {
            D->loop_s += j;
            D->loop_e += j;
        }
    }
    D->armed_j = j;
    D->armed_b = b;
    dmb();
    D->armed_jump = seq;
}

/* A new loop or a new size, as Mixxx's LoopingControl (slotBeatLoop, findQuantizedBeatloopStart):
 *   new loop  starts on the beat closest to t (a fraction of the beat for loops under one); one just
 *             ahead is a catching loop, the playhead runs into it
 *   new size  keeps the loop's start, the end moves; users past the new end go back in whole sizes
 *             (adjustedPositionInsideAdjustedLoop): a halved loop jumps into its first half
 * With the song on, positions are the song's in its scene (after its own loop has taken it back), and
 * a window past the scene's end is moved back to end there: it never runs into the next scene.
 * loop_a is the loop's start in transport ticks for users joining now; users already looping move
 * from where they are in their loop (loop_delta: how far the start moved). */
static long long round_to(long long x, long long g)
{
    return floor_div(x + g / 2, g) * g;
}

static void arm_loop(long long t)
{
    unsigned seq = D->loop_seq;
    int len = D->loop_ticks, resize = D->loop_len > 0 && len > 0;
    long long g = len < TICKS_BEAT ? len : TICKS_BEAT;
    D->loop_delta = 0;
    if (len > 0 && song_on()) {
        long long slen = (long long)D->song_count * TICKS_BEAT, p = t - song_start(), pc, ls, le;
        int had = resize && D->loop_e > D->loop_s, len0 = D->loop_e - D->loop_s;
        if (had && p >= D->loop_e)       /* the song's own loop takes it back first */
            p = D->loop_s + (p - D->loop_s) - LDIV(p - D->loop_s, len0) * len0;
        pc = p < 0 ? 0 : p >= slen ? slen - 1 : p;
        ls = had ? D->loop_s : round_to(pc, g);
        le = ls + len;
        if (le > slen) {                 /* cut on the scene's end: grows back, before the playhead */
            le = slen;
            ls = le - len < 0 ? 0 : le - len;
        }
        if (had)
            D->loop_delta = (int)(ls - D->loop_s);
        resize = had;                    /* a loop from before the song played is placed anew */
        D->loop_a = t - (p - ls);
        D->loop_len = (int)(le - ls);
        D->loop_s = (int)ls;
        D->loop_e = (int)le;
    } else if (len > 0) {
        if (resize && t > D->loop_a)     /* the start of the pass the loop is in */
            D->loop_a += floor_div(t - D->loop_a, D->loop_len) * D->loop_len;
        else if (!resize)
            D->loop_a = round_to(t, g);
        D->loop_s = D->loop_e = 0;
        D->loop_len = len;
    } else {
        D->loop_s = D->loop_e = 0;
        D->loop_len = 0;
    }
    D->loop_resize = (unsigned char)resize;
    D->loop_from = t;
    dmb();
    D->armed_loop = seq;
}

/* The first wrap at loop_a + k * lp (k >= 1) at or after t. */
static long long next_wrap(long long t, int lp)
{
    long long k = ceil_div(t - D->loop_a, lp);
    return D->loop_a + (k < 1 ? 1 : k) * lp;
}

/* A user's move at its unit / step starting at tick t, q ticks long: a multiple of q, in ticks. */
static long long dj_shift(DjUser *u, long long t, int q)
{
    long long s = 0;
    int switching;
    if (D->armed_jump != D->jump_seq)
        arm_jump(t);
    if (D->armed_loop != D->loop_seq)
        arm_loop(t);
    if (u->jump_seen != D->armed_jump && t >= D->armed_b) {
        u->jump_seen = D->armed_jump;
        s += (long long)(D->armed_j / q) * q;
    }
    switching = u->loop_seen != D->armed_loop && t >= D->loop_from;
    if (!switching && u->lp && t >= u->wrap_at) {
        if (t - u->wrap_at < q) {        /* the unit / step at the loop's end: back to its start */
            s -= u->lp;
            u->wrap_at += u->lp;
        } else {
            u->wrap_at = next_wrap(t, u->lp);   /* not played for a while */
        }
    }
    if (switching) {                     /* a new loop or size: from its first unit / step at loop_from */
        long long lp0 = u->lp, d;
        u->loop_seen = D->armed_loop;
        u->lp = 0;
        if (D->loop_len > 0) {
            int lp = (D->loop_len + q - 1) / q * q;
            if (lp0 && D->loop_resize) { /* from where it is in its loop, after a wrap due now */
                long long pos = t - (u->wrap_at - lp0), post = pos;
                if (pos >= 0)
                    post = pos - floor_div(pos, lp0) * lp0;
                s -= pos - post;
                d = post - D->loop_delta;
            } else {
                d = t - D->loop_a;
            }
            if (d >= 0) {                /* past the end: back in whole sizes */
                long long k = floor_div(d, lp);
                s -= k * lp;
                d -= k * lp;
            }
            u->wrap_at = t - d + lp;     /* d < 0: a catching loop, its start still ahead */
            u->lp = lp;
        }
    }
    return s;
}

static int wrap_into(int v, int lo, int hi)
{
    int span = hi - lo;
    v = (v - lo) % span;
    return lo + (v < 0 ? v + span : v);
}

/* deck.c deck_note_unit: a Clip that starts now does not take a jump or join a loop asked before,
 * armed yet or not (arming sets armed_* to the request's sequence number). */
void dj_clip_start(unsigned pad)
{
    DjUser *u;
    if (!live() || pad >= DJ_PADS)
        return;
    u = &D->clip[pad];
    u->jump_seen = D->jump_seq;
    u->loop_seen = D->loop_seq;
    u->lp = 0;
}

/* deck.c deck_wrap_unit: the unit a Clip on a note-value Sync grid plays next (ut ticks each), moved
 * by a jump or a loop wrap. Played within [lo, hi): its region or the whole file, as a jump past the
 * end goes round. */
unsigned dj_clip_unit(unsigned pad, unsigned unit, long long tick, unsigned ut, unsigned lo, unsigned hi)
{
    long long s;
    if (!live() || pad >= DJ_PADS || !ut || hi <= lo)
        return unit;
    s = dj_shift(&D->clip[pad], tick, (int)ut);
    if (!s)
        return unit;
    return (unsigned)wrap_into((int)unit + (int)LDIV(s, ut), (int)lo, (int)hi);
}

/* deck.c: the unit a jump this Clip has not taken yet will land on (for the prefetch), or ~0. `unit`
 * plays at `tick`; the jump is taken at armed_b. */
unsigned dj_clip_ahead(unsigned pad, unsigned unit, long long tick, unsigned ut, unsigned lo, unsigned hi)
{
    DjUser *u;
    int k;
    if (!live() || pad >= DJ_PADS || !ut || hi <= lo)
        return ~0u;
    u = &D->clip[pad];
    if (u->jump_seen == D->armed_jump || D->armed_jump != D->jump_seq || !D->armed_j)
        return ~0u;
    k = D->armed_j / (int)ut;
    if (!k)
        return ~0u;
    k += (int)LDIV(D->armed_b - tick, ut);
    return (unsigned)wrap_into((int)unit + k, (int)lo, (int)hi);
}

static DjUser *seq_user(void *p)
{
    unsigned i;
    for (i = 0; i < DJ_PLAYERS; i++)
        if (D->player[i] == p)
            return &D->seq[i];
    for (i = 0; i < DJ_PLAYERS; i++)
        if (!D->player[i]) {
            DjUser *u = &D->seq[i];
            D->player[i] = p;
            u->jump_seen = D->armed_jump;
            u->loop_seen = ~D->armed_loop;    /* follows the loop as it is now */
            u->lp = 0;
            return u;
        }
    return 0;
}

/* A pad sequence moved to `target`: its notes play from there. Stock fires a layer's events by
 * position from the pass start (layer+0x38, ticks) with a cursor (layer+0x14) into its sorted event
 * list (layer+0xC: index table, count, cursor, -, pool), and only step 0 resets them; so the start
 * goes back by target steps from this one and the cursor to the first event at or after it. In step
 * mode (+0x5E) an event's position counts steps (960 each) of the current step length. */
static void seq_seek(void *p, int target, long long t, long long q)
{
    void *layer = (void *)u32(p, 0xC20);
    const unsigned short *idx;
    const char *pool;
    unsigned i, n;
    long long at_pos = (long long)target * q;
    if (!layer)
        return;
    *(long long *)at(layer, 0x38) = t - at_pos;
    idx = (const unsigned short *)u32(layer, 0xC);
    n = u32(layer, 0x10);
    pool = (const char *)(u32(layer, 0x1C) ? u32((const void *)u32(layer, 0x1C), 0) : 0);
    for (i = 0; i < n && idx && pool; i++) {
        long long e = (long long)u32(pool + (idx[i] & 0x3FFF) * 0x18, 4);
        if (u8(p, 0x5E))
            e = (long long)(unsigned)((unsigned long long)e * 0x88888889u >> 41) * q;   /* e / 960 */
        if (e >= at_pos)
            break;
    }
    *(unsigned *)at(layer, 0x14) = i;
}

/* Player_Tick (0x0805DD34, hook_dj_step): the step after `step` is about to start. csp = Player_Tick's
 * sp: [sp+0x20] / [sp+0x14] this step's tick (low / high), [sp+0x50] / [sp+0x54] the step length.
 * Returns the step stock then adds 1 to. A sequence goes round its `count` steps; the song stays in
 * its scene (the jump was checked against it), and a step the song is moved to does not count as a
 * scene start: its song flag is off until the step's events have run (dj_seq_done). */
int dj_seq_step(void *p, int step, int count, const unsigned *csp)
{
    long long t = (long long)((unsigned long long)csp[0x14 / 4] << 32 | csp[0x20 / 4]);
    long long q = (long long)((unsigned long long)csp[0x54 / 4] << 32 | csp[0x50 / 4]);
    int next = step + 1, song, target;
    long long s;
    DjUser *u;
    if (!live() || step < 0 || count < 1 || q <= 0 || q > 0x7FFFFFFF)
        return step;
    song = u8(p, 0x60) != 0 || (p == D->song && D->song_forced);   /* its flag is ours to give back */
    if (song)
        D->song = p;
    if (!(u = seq_user(p)))
        return step;
    s = dj_shift(u, t, (int)q);          /* arms against where the song was: its step is set after */
    target = next + (int)LDIV(s, q);
    if (song) {
        D->song_tick = t;
        if (s && target >= 0 && target < count) {
            D->song_step = target;
            D->song_count = count;
            *(unsigned char *)at(p, 0x60) = 0;
            D->song_forced = 1;
            return target - 1;
        }
        if (next < count) {              /* a wall the jump was not checked against: stay */
            D->song_step = next;
            D->song_count = count;
        } else {
            D->song_step = 0;            /* a new scene: its length once the layer is switched */
            D->song_count = 0;
        }
        return step;
    }
    if (!s)
        return step;
    target = wrap_into(target, 0, count);
    if (target != wrap_into(next, 0, count))
        seq_seek(p, target, t, q);
    return target - 1;
}

/* After the step's events (0x0805DDDC, hook_dj_step0): the song flag back, and a new scene's length. */
void dj_seq_done(void *p)
{
    void *layer;
    if (!live() || p != D->song)
        return;
    if (D->song_forced) {
        *(unsigned char *)at(p, 0x60) = 1;
        D->song_forced = 0;
    }
    if (D->song_count == 0 && (layer = (void *)u32(p, 0xC20)) != 0) {
        int n = (int)u32(layer, 4);
        D->song_count = n < 1 ? 1 : n > 512 ? 512 : n;
    }
}

/* ---- Song screen (UI task) ---- */

static void set_rect(void *btn, int x, int y, int w, int h)
{
    int r[4];
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
    BTN_RECT(btn, r);
}

/* As the oldest child of the Song widget: drawn last (over the scene list) and hit first. */
static void add_oldest(void *parent, void *w)
{
    void *old = (void *)u32(parent, 0x18);
    *(void **)at(w, 0x20) = old;
    *(void **)at(w, 0x24) = 0;
    if (old)
        *(void **)at(old, 0x24) = w;
    else
        *(void **)at(parent, 0x1C) = w;
    *(void **)at(parent, 0x18) = w;
    *(void **)at(w, 0x2C) = parent;
}

/* ---- the deck view: widgets and drawing ---- */

typedef void (*fill_fn)(const int *, unsigned, void *);
typedef void (*draw_fn)(void *, const void *);
typedef const char *(*cstr_fn)(const void *);
#define FILL_RECT   ((fill_fn)0x0808EA23u)     /* (rect, palette index, canvas) */
#define OUTLINE     ((fill_fn)0x0808E995u)
#define BTN_DRAW    ((draw_fn)0x080A3B6Du)     /* button vt+0x04 */
#define STR_CSTR    ((cstr_fn)0x080460BBu)     /* a str object's chars */
#define PAL_BG      2          /* 0x141414 */
#define PAL_WHITE   3
#define PAL_PINK    4          /* 0xFF6AF3: the project */
#define PAL_CYAN    5          /* 0x09D7F5: the loop */
#define PAL_DEAD    25         /* 0x2B2B2B: crossfader ends, one side silent */
#define PAL_TRACK   17         /* 0x707070 */
#define PAL_EDGE    1          /* 0x56565A */
#define PAL_TICK    16         /* 0x444444: bar ticks */
#define XF_MARGIN   12
#define XF_KNOB     16
#define XF_DEAD_PCT 5          /* as mixer.c XF_DEAD: the ends where one side is silent */
#define SL_MARGIN   8
#define SL_KNOB     10
#define SL_SNAP     40         /* a slider dragged this close to the centre is Off */
#define WAVE_MS     50         /* the waveform repaints at most this often */
#define WAVE_COLS   160        /* columns per lane (2 px each over 320) */

static void rect(int *r, int x, int y, int w, int h)
{
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
}

static void *canvas(const void *ctx) { return *(void *const *)((const char *)ctx + 4); }
static int wx(const void *w) { return (int)u32(w, 4); }
static int wy(const void *w) { return (int)u32(w, 8); }
static int ww(const void *w) { return (int)u32(w, 0xC); }
static int wh(const void *w) { return (int)u32(w, 0x10); }

/* A widget of ours paints again. The GUI only runs a pass when a screen tick raises its byte (the
 * tick's flags: [0] full, [1] partial), and stock's Song tick knows nothing of our widgets, so
 * dj_song_tick raises it for them (X->ask). */
static void dirty(void *w)
{
    *(unsigned char *)at(w, 0x31) = 1;
    *(unsigned char *)at(w, 0x32) = 1;
    X->ask |= 1;
}

static void no_touch(void *w, const int *pt)
{
    (void)w;
    (void)pt;
}

static void no_up(void *w)
{
    (void)w;
}

/* The crossfader: touches set it from x. */
static void xf_touch(void *w, const int *pt)
{
    int x = pt[0] - wx(w) - XF_MARGIN, span = ww(w) - 2 * XF_MARGIN;
    if (mx_state() != 2 || span <= 0)
        return;
    mx_set_xf(x <= 0 ? 0 : x >= span ? 1000 : x * 1000 / span);
}

/* A filter slider: -1000 (left, low-pass) .. 1000 (right, high-pass), Off near the centre. */
static unsigned slider_side(const void *w) { return u16(w, 0x14) == ID_SL0 ? 1 : 0; }

static void sl_touch(void *w, const int *pt)
{
    int x = pt[0] - wx(w) - SL_MARGIN, span = ww(w) - 2 * SL_MARGIN, v;
    if (span <= 0)
        return;
    v = (x <= 0 ? 0 : x >= span ? span : x) * 2000 / span - 1000;
    mx_set_filter(slider_side(w), v > -SL_SNAP && v < SL_SNAP ? 0 : v);
}

/* Panel: the deck's background over the hidden scene list and top bar; it takes stray touches. */
static void panel_draw(void *w, const void *ctx)
{
    int r[4];
    rect(r, wx(w), wy(w), ww(w), wh(w));
    FILL_RECT(r, PAL_BG, canvas(ctx));
}

/* Scene prev / next: a stock button with a solid arrow painted on it (no font glyph needed). */
static void arrow_draw(void *w, const void *ctx)
{
    void *cv = canvas(ctx);
    int up = u16(w, 0x14) == ID_UP, cx = wx(w) + ww(w) / 2, cy = wy(w) + wh(w) / 2, k, r[4];
    BTN_DRAW(w, ctx);
    for (k = 0; k < 9; k++) {            /* rows of 1, 3, .. 17 px: apex up or down */
        rect(r, cx - k, up ? cy + 4 - k : cy - 4 + k, 2 * k + 1, 1);
        FILL_RECT(r, PAL_WHITE, cv);
    }
}

static void slider_draw(void *w, const void *ctx)
{
    void *cv = canvas(ctx);
    unsigned side = slider_side(w), on = (unsigned)mx_filter_on(side);
    int v = mx_filter(side), x = wx(w), y = wy(w), h = wh(w), tw = ww(w) - 2 * SL_MARGIN;
    int tx = x + SL_MARGIN, mid = tx + tw / 2, kx = tx + (v + 1000) * tw / 2000, r[4];
    rect(r, x, y, ww(w), h);
    FILL_RECT(r, PAL_BG, cv);
    rect(r, tx, y + h / 2 - 2, tw, 4);
    FILL_RECT(r, PAL_TRACK, cv);
    if (kx != mid) {                     /* from the centre to the knob, in the side's colour */
        rect(r, kx < mid ? kx : mid, y + h / 2 - 2, kx < mid ? mid - kx : kx - mid, 4);
        FILL_RECT(r, !on ? PAL_EDGE : side ? PAL_CYAN : PAL_PINK, cv);   /* grey while out */
    }
    rect(r, mid, y + 3, 1, h - 6);
    FILL_RECT(r, PAL_EDGE, cv);
    rect(r, kx - SL_KNOB / 2, y + 4, SL_KNOB, h - 8);
    FILL_RECT(r, on ? PAL_WHITE : PAL_TRACK, cv);
    OUTLINE(r, PAL_EDGE, cv);
    X->sl_drawn[side ? 0 : 1] = v * 2 + (int)on;
}

/* Without an audio loop: the stock button with the capture hint. With one: a track (dark at both
 * ends, where one side is silent), the knob, and the loop's (cyan, left) and the project's (pink,
 * right) level as bars at the edges. */
static void xf_draw(void *w, const void *ctx)
{
    void *cv = canvas(ctx);
    int x = wx(w), y = wy(w), wd = ww(w), h = wh(w);
    int r[4], tx = x + XF_MARGIN, tw = wd - 2 * XF_MARGIN, dz = tw * XF_DEAD_PCT / 100, pos, gl, gr;
    if (mx_state() != 2 || tw <= 0) {
        BTN_DRAW(w, ctx);
        return;
    }
    pos = mx_xf();
    mx_gains(&gl, &gr);
    rect(r, x, y, wd, h);
    FILL_RECT(r, PAL_BG, cv);
    rect(r, tx, y + h / 2 - 3, tw, 6);
    FILL_RECT(r, PAL_TRACK, cv);
    rect(r, tx, y + h / 2 - 3, dz, 6);
    FILL_RECT(r, PAL_DEAD, cv);
    rect(r, tx + tw - dz, y + h / 2 - 3, dz, 6);
    FILL_RECT(r, PAL_DEAD, cv);
    rect(r, x + 1, y + 2, 6, (h - 4) * gl / 1000);
    if (r[3] > 0)
        FILL_RECT(r, PAL_CYAN, cv);
    rect(r, x + wd - 7, y + 2, 6, (h - 4) * gr / 1000);
    if (r[3] > 0)
        FILL_RECT(r, PAL_PINK, cv);
    rect(r, tx + tw * pos / 1000 - XF_KNOB / 2, y + 4, XF_KNOB, h - 8);
    FILL_RECT(r, PAL_WHITE, cv);
    OUTLINE(r, PAL_EDGE, cv);
    rect(r, tx + tw * pos / 1000, y + 6, 1, h - 12);
    FILL_RECT(r, PAL_BG, cv);
    X->drawn = pos;
}

/* One lane: a column of 2 px per peak, mirrored around its middle. */
static void lane(void *cv, int x, int y, int h, const unsigned char *pk, int n, unsigned colour)
{
    int i, r[4];
    for (i = 0; i < n; i++) {
        int a = pk[i] * (h - 2) / 2 / 255;
        if (!a)
            continue;
        rect(r, x + 2 * i, y + h / 2 - a, 2, 2 * a);
        FILL_RECT(r, colour, cv);
    }
}

/* The waveform: the loop lane on top (the audio loop, or the DJ loop being recorded, with bar ticks
 * and the playhead), the project lane below (the last ~1.7 s, newest on the right). */
static void wave_draw(void *w, const void *ctx)
{
    void *cv = canvas(ctx);
    DjDeck *d = X->deck;
    int x = wx(w), y = wy(w), h = wh(w), lh = h / 2, n = ww(w) / 2, head, len, r[4];
    long long start, b;
    if (n > WAVE_COLS)
        n = WAVE_COLS;
    rect(r, x, y, ww(w), h);
    FILL_RECT(r, PAL_BG, cv);
    rect(r, x, y + lh, ww(w), 1);
    FILL_RECT(r, PAL_TICK, cv);
    head = mx_wave_loop(d->lane[0], n, D->loop_len > 0 ? D->loop_len : loop_t[D->ui.loop_idx], &start, &len);
    if (head != -2) {
        for (b = floor_div(start, 3840) * 3840; b < start + len; b += 3840)
            if (b >= start) {            /* a tick on every bar of the transport */
                rect(r, x + (int)LDIV((b - start) * 2 * n, len), y + lh + 1, 1, lh - 1);
                FILL_RECT(r, PAL_TICK, cv);
            }
        lane(cv, x, y + lh + 1, lh - 1, d->lane[0], n, PAL_CYAN);
        mx_wave_done();
        if (head >= 0) {
            rect(r, x + 2 * head, y + lh + 1, 1, lh - 1);
            FILL_RECT(r, PAL_WHITE, cv);
        }
    }
    mx_wave_proj(d->lane[1], n);
    lane(cv, x, y, lh, d->lane[1], n, PAL_PINK);
}

#define VT(draw, down, move, up) {                                                            \
    (const void *)0x080A3D03u, (const void *)(draw), (const void *)0x080AEB1Du,               \
    (const void *)0x080AEBABu, (const void *)(down), (const void *)(move), (const void *)(up), \
    (const void *)0x080A3323u, (const void *)0x080A3759u, (const void *)0x080A3317u,          \
    (const void *)0x080AEB5Fu, (const void *)0x080A331Bu, (const void *)0x080AEC05u,          \
    (const void *)0x080A3321u, (const void *)0x080A3773u, (const void *)0x080A3979u }
/* Copies of the stock button vtable 0x080EFE48: draw (+0x04) and touches (+0x10/+0x14/+0x18). */
static const void *const xf_vt[16] = VT(xf_draw, xf_touch, xf_touch, no_up);
static const void *const panel_vt[16] = VT(panel_draw, no_touch, no_touch, no_up);
static const void *const slider_vt[16] = VT(slider_draw, sl_touch, sl_touch, no_up);
static const void *const wave_vt[16] = VT(wave_draw, no_touch, no_touch, no_up);
static const void *const arrow_vt[16] = VT(arrow_draw, 0x080A3835u, 0x080A3315u, 0x080A3899u);

/* A widget of ours on the Song widget: the oldest child, so it is drawn after (over) stock's and hit
 * before it. vt 0 = the stock button. */
static void place(void *song, void *b, const void *const *vt, unsigned id, int x, int y, int w, int h)
{
    BTN_CTOR(b);
    if (vt)
        *(const void *const **)b = vt;
    ((set_id_fn)(*(unsigned **)b)[9])(b, id);
    BTN_LATCH(b, 1);
    add_oldest(song, b);
    set_rect(b, x, y, w, h);
    HIDE(b, 1);
}

static const char s_cap_loop[] = "Loop";
static const char s_cap_proj[] = "Proj";

/* The deck view, y bottom-up (stock's header 222-240 stays):
 *   182-220  << | >> | ^ | v | Loop          (64 | 64 | 36 | 36 | 120; Loop above the top-right knob)
 *   164-180  scene line
 *    78-162  waveform: loop lane over project lane
 *    46- 76  Loop [filter] | Proj [filter]
 *     0- 44  crossfader */
static void build(void *song)
{
    static const int x[NBTN] = {0, 64, 200}, w[NBTN] = {64, 64, 120};   /* Loop: "Audio 1/16" fits */
    DjDeck *d = X->deck;
    unsigned i;
    D->ui.song = song;
    D->ui.lbl_jump = D->ui.lbl_loop = 0xFF;
    X->drawn = X->sl_drawn[0] = X->sl_drawn[1] = -2000;
    if (!d)
        return;                          /* no deck memory: the row is never shown */
    place(song, d->panel, panel_vt, ID_PANEL, 0, 0, 320, 221);
    for (i = 0; i < NBTN; i++)
        place(song, D->btn[i], 0, ID_DJ + i, x[i], 182, w[i], 38);
    place(song, d->up, arrow_vt, ID_UP, 128, 182, 36, 38);
    place(song, d->dn, arrow_vt, ID_DN, 164, 182, 36, 38);
    place(song, d->line, 0, ID_LINE, 0, 163, 320, 18);
    place(song, d->wave, wave_vt, ID_WAVE, 0, 78, 320, 84);
    place(song, d->cap[0], 0, ID_CAP0, 0, 46, 72, 30);    /* a stock label shows the tail of a */
    place(song, d->sl[0], slider_vt, ID_SL0, 72, 46, 88, 30);    /* text too wide: "Loop" needs ~72 */
    place(song, d->cap[1], 0, ID_CAP1, 160, 46, 72, 30);
    place(song, d->sl[1], slider_vt, ID_SL1, 232, 46, 88, 30);
    place(song, X->btn, xf_vt, XF_ID, 0, 0, 320, 44);
    BTN_TEXT(d->cap[0], s_cap_loop);
    BTN_TEXT(d->cap[1], s_cap_proj);
    X->line[0] = 0;
}

static void cat(char *d, const char *a, const char *b)
{
    while (*a)
        *d++ = *a++;
    while (*b)
        *d++ = *b++;
    *d = 0;
}

static int size_idx(int ticks)
{
    unsigned i;
    for (i = 0; i < LOOP_SIZES; i++)
        if (loop_t[i] >= ticks)
            return (int)i;
    return LOOP_SIZES - 1;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Our text on a stock button, again only when it shows something else: BTN_TEXT also lays the
 * label out again and repaints it, so setting the same text every tick jammed the labels. The label
 * is embedded at +0x34, its text at label +0x31 (ui_draw_cstring), i.e. button +0x65. */
static void keep_text(void *btn, const char *t)
{
    if (!u32(btn, 0))                /* not built (yet) */
        return;
    if (!same((const char *)at(btn, BTN_TEXT_AT), t)) {
        BTN_TEXT(btn, t);
        dirty(btn);                  /* the button's face first: a label alone paints its new text */
        X->ask |= 3;                 /* over the old one. Both passes, as the Mix screen does */
    }
}

static void set_lit(void *btn, unsigned on)
{
    if (u8(btn, 0x96) != on) {
        BTN_STATE(btn, on);
        X->ask |= 1;
    }
}

static void set_text(void *btn, const char *t)
{
    BTN_TEXT(btn, t);
    dirty(btn);
    X->ask |= 3;
}

/* "3 Drop  > 4 Break": the scene playing (or last pressed with the song off) and the one queued or
 * next. Names are cut to 9 characters so the line fits 320 px. */
static char *scene_text(char *o, unsigned i)
{
    const void *str = (const void *)(APP + SCENE_TAB + i * SCENE_SIZE + SCENE_NAME);
    const char *nm = STR_CSTR(str);
    unsigned k = 0;
    if (i >= 9)
        *o++ = (char)('0' + (i + 1) / 10);
    *o++ = (char)('0' + (i + 1) % 10);
    *o++ = ' ';
    while (nm && nm[k] && k < 9)
        *o++ = nm[k++];
    *o = 0;
    return o;
}

static void line_text(char *t)
{
    int count = PARAM_GET(SESSION_BAG, ID_SCENES), cur = (int)APP_SCENE, next = -1;
    void *p = D->song;
    char *o = t;
    if (song_on()) {
        void *lay = (void *)u32(p, PL_LAYER), *q = (void *)u32(p, PL_QUEUED);
        if (lay)
            cur = (int)u16(lay, LAY_INDEX);
        next = q && q != lay ? (int)u16(q, LAY_INDEX) : cur + 1 < count ? cur + 1 : -1;
    }
    if (cur < 0 || cur >= count) {
        *t = 0;
        return;
    }
    o = scene_text(o, (unsigned)cur);
    if (next >= 0 && next < count) {
        cat(o, s_next, "");
        o += 4;
        scene_text(o, (unsigned)next);
    }
}

static void labels(void *song)
{
    DjUi *ui = &D->ui;
    DjDeck *d = X->deck;
    int audio = mx_state() != 0, st = mx_state();
    unsigned char want = (unsigned char)(audio ? size_idx(mx_len()) | 0x80 : ui->loop_idx);
    (void)song;
    if (ui->lbl_jump != ui->jump_idx) {
        cat(ui->text[0], s_back, jump_n[ui->jump_idx]);
        cat(ui->text[1], jump_n[ui->jump_idx], s_fwd);
        set_text(D->btn[0], ui->text[0]);
        set_text(D->btn[1], ui->text[1]);
        ui->lbl_jump = ui->jump_idx;
    }
    if (ui->lbl_loop != want) {
        cat(ui->text[2], audio ? s_audio : s_loop, loop_n[want & 0x7F]);
        set_text(D->btn[2], ui->text[2]);
        ui->lbl_loop = want;
    }
    set_lit(D->btn[0], 0);     /* a press may have lit them */
    set_lit(D->btn[1], 0);
    set_lit(D->btn[2], ui->loop_on || audio);
    line_text(X->line);
    keep_text(d->line, X->line);
    if (st != 2)                     /* the capture hint: BACK only once the pass is recorded */
        keep_text(X->btn, st ? s_recording : !ui->loop_on ? s_loop_first
                  : !mx_have() ? s_no_ring : mx_ready(D->loop_a, D->loop_len) ? s_capture : s_recording);
    else
        keep_text(X->btn, "");       /* xf_draw paints it; no text */
    if (X->drawn != mx_xf())
        dirty(X->btn);
    set_lit(d->cap[0], (unsigned)mx_filter_on(1));   /* lit while the filter is in */
    set_lit(d->cap[1], (unsigned)mx_filter_on(0));
    if (X->sl_drawn[0] != mx_filter(1) * 2 + mx_filter_on(1))
        dirty(d->sl[0]);
    if (X->sl_drawn[1] != mx_filter(0) * 2 + mx_filter_on(0))
        dirty(d->sl[1]);
    if (UI_TICK - X->wave_ms >= WAVE_MS) {
        X->wave_ms = UI_TICK;
        dirty(d->wave);
    }
}

static void *row_of(void *song, unsigned scene)
{
    unsigned i;
    for (i = 0; i < S_NROWS; i++) {
        void *r = at(song, S_ROWS + i * S_ROW_SIZE);
        if (u32(r, ROW_SCENE) == scene && !u8(r, W_HIDDEN))
            return r;
    }
    return 0;
}

static void rows_dirty(void *song)
{
    unsigned i;
    for (i = 0; i < S_NROWS; i++) {
        void *r = at(song, S_ROWS + i * S_ROW_SIZE);
        *(unsigned char *)at(r, 0x31) = 1;
        *(unsigned char *)at(r, 0x32) = 1;
    }
}

/* A refused jump: the scene it would have reached blinks three times (in the list, once it is back). */
static void blink(void *song)
{
    DjUi *ui = &D->ui;
    void *r;
    unsigned el;
    if (ui->refuse_seen != D->refuse_seq) {
        ui->refuse_seen = D->refuse_seq;
        if (ui->blink != NO_SCENE && (r = row_of(song, ui->blink)) != 0)
            ROW_LIT(r, u32(song, S_SCENE) == ui->blink);
        ui->blink = D->refuse_scene;
        ui->blink_t0 = UI_TICK;
    }
    if (ui->blink == NO_SCENE)
        return;
    r = row_of(song, ui->blink);
    el = UI_TICK - ui->blink_t0;
    if (el >= BLINK_MS) {
        if (r)
            ROW_LIT(r, u32(song, S_SCENE) == ui->blink);
        ui->blink = NO_SCENE;
        return;
    }
    if (r)
        ROW_LIT(r, (el / BLINK_HALF) % 2 == 0);
}

/* SONG blinks while the row is shown, and is lit again (as on the Song screen) when it goes. */
static void led(unsigned show)
{
    unsigned char want = show ? ((UI_TICK / LED_HALF) & 1 ? 2 : 1) : 0;
    if (want == D->ui.led)
        return;
    LED_SET(LEDS, LED_SONG, want != 2);
    D->ui.led = want;
}

static void deck_hide(unsigned hidden)
{
    DjDeck *d = X->deck;
    unsigned i;
    void *w[] = {d->panel, D->btn[0], D->btn[1], D->btn[2], d->up, d->dn, d->line, d->wave,
                 d->cap[0], d->sl[0], d->cap[1], d->sl[1], X->btn};
    for (i = 0; i < sizeof(w) / sizeof(w[0]); i++)
        HIDE(w[i], hidden);
}

/* Every label lays its text out again when the deck opens. A stock label scrolls a text it thinks
 * too wide and keeps that offset while hidden: the second open showed "op" / "oj" / "op 1" and lost
 * "<< 16" until a knob set the text again. Setting it again starts the label from its first letter. */
static void relabel(void)
{
    DjDeck *d = X->deck;
    D->ui.lbl_jump = D->ui.lbl_loop = 0xFF;   /* labels() sets the top bar again */
    set_text(d->cap[0], s_cap_loop);
    set_text(d->cap[1], s_cap_proj);
    set_text(d->line, X->line);
    set_text(X->btn, "");                     /* labels() puts the hint back */
}

/* Every widget under w (and w) paints again in both passes: what HIDE alone does not do for the
 * children of a container coming back. Children hang off +0x18 (oldest), linked on through +0x20. */
static void dirty_tree(void *w)
{
    void *stack[12];
    int sp = 0;
    stack[sp++] = w;
    while (sp) {
        void *c = stack[--sp];
        for (; c; c = (void *)u32(c, 0x20)) {
            void *kid = (void *)u32(c, 0x18);
            dirty(c);
            if (kid && sp < 12)
                stack[sp++] = kid;
            if (c == w)
                break;                   /* w's own siblings are not ours to touch */
        }
    }
}

/* Stock's scene list and top bar go while the deck is shown (they would paint over it) and come
 * back as they were. */
static void stock_hide(void *song, unsigned hide)
{
    void *list = at(song, S_LIST), *bar = at(song, S_BAR);
    if (hide) {
        X->hid_list = (unsigned char)u8(list, W_HIDDEN);
        X->hid_bar = (unsigned char)u8(bar, W_HIDDEN);
        HIDE(list, 1);
        HIDE(bar, 1);
    } else {
        HIDE(list, X->hid_list);
        HIDE(bar, X->hid_bar);
        rows_dirty(song);
        dirty_tree(song);                /* the whole screen paints again, not only the containers */
    }
}

static void ui_update(void *song)
{
    DjUi *ui = &D->ui;
    unsigned show;
    if (ui->song != song)
        build(song);
    show = ui->want && X->deck && !u8(song, S_MODE);
    if (show != ui->shown) {
        stock_hide(song, show);
        deck_hide(!show);
        ui->shown = (unsigned char)show;
        X->ask |= 3;                     /* the whole screen changes: both passes */
        if (show)
            relabel();
    }
    if (show) {
        if (!u8(at(song, S_LIST), W_HIDDEN))
            HIDE(at(song, S_LIST), 1);   /* stock showed it again (a refresh) */
        if (!u8(at(song, S_BAR), W_HIDDEN))
            HIDE(at(song, S_BAR), 1);
        labels(song);
    }
    led(show);
    blink(song);
}

/* App_HandleInput, SONG (0x080A329E): on the Song screen it shows / hides the deck instead. */
void dj_song_key(void *app, unsigned screen, unsigned a, unsigned b)
{
    if (live() && u8(app, APP_SCREEN) == SCR_SONG) {
        D->ui.want ^= 1;
        return;
    }
    if (live())
        D->ui.want = 0;
    SET_SCREEN(app, screen, a, b);
}

/* Song vtable +0x00. */
void dj_song_tick(void *song, void *flags, void *a, void *b)
{
    if (live())
        ui_update(song);
    SONG_TICK(song, flags, a, b);
    if (live() && X->ask && flags) {     /* our widgets changed: ask the GUI for the pass(es) */
        if (X->ask & 2)
            ((unsigned char *)flags)[0] = 1;
        ((unsigned char *)flags)[1] = 1;
        X->ask = 0;
    }
}

static void request_jump(int dir)
{
    D->jump_ticks = dir * jump_t[D->ui.jump_idx];
    D->jump_ms = UI_TICK;
    dmb();
    D->jump_seq++;
}

static void request_loop(void)
{
    D->loop_ticks = D->ui.loop_on ? loop_t[D->ui.loop_idx] : 0;
    dmb();
    D->loop_seq++;
}

/* Scene ^ / v: the scene before / after the one last pressed (or queued), as a row press would. */
static void scene_step(void *song, int dir)
{
    int count = PARAM_GET(SESSION_BAG, ID_SCENES), next = (int)APP_SCENE + dir;
    unsigned ev[6], j;
    if (next < 0 || next >= count)
        return;
    for (j = 0; j < 6; j++)
        ev[j] = 0;
    ev[0] = EV_SCENE;
    ev[1] = EV_TAG;
    ev[3] = (unsigned)next;
    UI_POST_UP(song, ev);
}

/* Song vtable +0x30: our buttons act on the press (as a DJ deck), the release is swallowed; the
 * deck's other widgets take nothing further. */
unsigned dj_song_child(void *song, const void *ev)
{
    unsigned type = u16(ev, 0), id = u32(ev, 0xC);
    if (!live() || (type != 1 && type != 2) || (id - ID_DJ >= NBTN && id != XF_ID && id - ID_PANEL >= N_DECK))
        return SONG_CHILD(song, ev);
    if (type == 1) {
        if (id == ID_DJ)
            request_jump(-1);
        else if (id == ID_DJ + 1)
            request_jump(1);
        else if (id == ID_DJ + 2) {
            if (mx_state()) {
                mx_clear();          /* Loop off ends the audio loop */
            } else {
                D->ui.loop_on ^= 1;
                request_loop();
            }
        } else if (id == ID_UP || id == ID_DN) {
            scene_step(song, id == ID_UP ? -1 : 1);
        } else if (id == ID_CAP0 || id == ID_CAP1) {
            unsigned side = id == ID_CAP0;   /* Loop / Proj: its filter in or out, the slider kept */
            mx_set_filter_on(side, !mx_filter_on(side));
        }
    }
    return 0;
}

static int steps_of(unsigned which, int delta)
{
    int *acc = &D->ui.acc[which], n;
    *acc += delta;
    n = *acc / STEP_COUNTS;
    *acc -= n * STEP_COUNTS;
    return n;
}

static unsigned char step_idx(unsigned char i, int n, unsigned count)
{
    int v = (int)i + n;
    return (unsigned char)(v < 0 ? 0 : v >= (int)count ? (int)count - 1 : v);
}

/* BACK with the deck shown and the DJ loop on, once the ring has recorded the pass playing now:
 * capture it as the audio loop. The DJ loop is switched off by the same request. 1 = taken. */
static int try_capture(void)
{
    long long a;
    int len;
    if (!D->ui.shown || !D->ui.loop_on || mx_state() || D->loop_len <= 0 ||
        D->armed_loop != D->loop_seq || !mx_ready(D->loop_a, D->loop_len))
        return 0;
    a = D->loop_a;                       /* the DJ loop as it plays, before it is switched off */
    len = D->loop_len;
    D->ui.loop_on = 0;
    request_loop();
    mx_capture(a, len, D->loop_seq);
    mx_set_filter(0, 0);                 /* a new loop starts with both filters centred and in */
    mx_set_filter(1, 0);
    mx_set_filter_on(0, 1);
    mx_set_filter_on(1, 1);
    X->facc[0] = X->facc[1] = 0;
    return 1;
}

/* App_HandleInput type 7 (hardware BACK, any screen; hook_type7 in ../3.1.9-folders-repitch/cave.S):
 * on the Song screen BACK captures. 0 = taken, anything else = stock's BACK. */
unsigned dj_back_key(void *app)
{
    if (live() && u8(app, APP_SCREEN) == SCR_SONG && try_capture())
        return 0;
    return 1;
}

static void filter_knob(unsigned side, int delta)
{
    int *acc = &X->facc[side], n;
    *acc += delta;
    n = *acc / FILT_STEP;
    *acc -= n * FILT_STEP;
    mx_set_filter(side, mx_filter(side) + n);
}

/* Song vtable +0x34: with the deck shown the knobs follow the screen: top-left the jump size, top-right
 * the loop size (x2 / /2 per step), bottom-left the loop filter, bottom-right the project filter.
 * Leaving the screen (0xD3) hides the deck. */
void dj_song_event(void *song, const void *ev)
{
    unsigned type = u16(ev, 0);
    if (live() && type == 0xD3)
        D->ui.want = 0;
    if (live() && type == EV_BACK && try_capture())
        return;
    if (live() && type == 0x32 && D->ui.shown) {
        unsigned k = u16(ev, 0xC);
        int delta = (short)u16(ev, 0x10), n;
        if (k == KNOB_BL || k == KNOB_BR) {
            filter_knob(k == KNOB_BL, delta);   /* mixer side: 1 loop, 0 project */
            return;
        }
        if (k == KNOB_TL) {
            n = steps_of(0, delta);
            D->ui.jump_idx = step_idx(D->ui.jump_idx, n, JUMP_SIZES);
            return;
        }
        if (k == KNOB_TR) {
            unsigned char was = D->ui.loop_idx;
            n = steps_of(1, delta);
            if (mx_state())
                was = (unsigned char)size_idx(mx_len());
            D->ui.loop_idx = step_idx(was, n, LOOP_SIZES);
            if (mx_state() && D->ui.loop_idx != was)
                mx_size(loop_t[D->ui.loop_idx]);   /* the audio loop takes it */
            else if (D->ui.loop_on && D->ui.loop_idx != was)
                request_loop();      /* the live loop takes the new size */
            return;
        }
    }
    SONG_EVENT(song, ev);
}
