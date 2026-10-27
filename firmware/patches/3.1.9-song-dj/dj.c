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
#define S_BTN_BPM   0x1049C    /* Song: top-left frame button (BPM) */
#define S_BTN_SWING 0x1053C    /* Song: top-right frame button (Swing) */
#define BTN_LABEL   0x34       /* button: its label widget; the text is at +0x65 */
#define S_BTN_EDIT  0x105DC    /* Song: Edit (id 0x27): Scene ^ while the row is shown */
#define S_BTN_LMODE 0x103FC    /* Song: Loop / Loop1 / LoopA (id 0x115): Scene v */
#define S_LMODE     0x12190    /* Song: its mode, 0..2 */
#define ID_EDIT     0x27
#define ID_LMODE    0x115
#define EDIT_TEXT   (*(const char *const *)0x080C316Cu)        /* stock's "Edit" */
#define LMODE_TEXT  ((const char *const *)0x080F1790u)         /* stock's three loop-mode labels */
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
#define EV_REFRESH  0x66       /* Song: read BPM / Swing again and label them */
#define FILT_STEP   8          /* encoder counts per filter step of 1 (of -1000..1000) */

/* The crossfader strip and the top labels: SRAM4 0x38006F00, free on 3.1.T between Song cues'
 * SgState (ends 0x38006C2C) and RbVoice (0x38007000). */
#define XF_BASE     0x38006F00u
#define XF_END      0x38007000u
#define XF_ID       0x1D3
typedef struct {
    unsigned btn[0x28];        /* +0x00 a stock button with our vtable (xf_vt) */
    int drawn;                 /* +0xA0 crossfader as last drawn, -1 = redraw */
    char bar[24];              /* +0xA4 */
    char top[2][16];           /* +0xBC top-left / top-right labels while the row is shown */
    int facc[2];               /* +0xDC encoder counts not yet a filter step */
    unsigned char tops;        /* +0xE4 top labels are ours */
    unsigned char rsv[3];
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
int mx_xf_target(void);
void mx_gains(int *l, int *r);
void mx_capture(long long a, int len, unsigned off_seq);
void mx_clear(void);
void mx_size(int ticks);
void mx_set_xf(int v);
void mx_set_filter(unsigned side, int v);

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
static const char s_off_f[] = "Off";
static const char s_scene_up[] = "Scene ^";
static const char s_scene_dn[] = "Scene v";
static const char s_proj[] = "Proj ";
static const char s_loopside[] = "Loop ";

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

/* The crossfader strip: a stock button (it draws its label) whose touches set the crossfader from
 * x instead of pressing it. Copy of the button vtable 0x080EFE48 with touchDown / touchMove / touchUp
 * ours. Rect {x, y, w, h} at +4; touch points are in the same frame. */
static void xf_touch(void *w, const int *pt)
{
    int x = pt[0] - (int)u32(w, 4) - 12, span = (int)u32(w, 0xC) - 24;   /* XF_MARGIN */
    if (mx_state() != 2 || span <= 0)
        return;
    mx_set_xf(x <= 0 ? 0 : x >= span ? 1000 : x * 1000 / span);
}

static void xf_up(void *w)
{
    (void)w;
}

typedef void (*fill_fn)(const int *, unsigned, void *);
typedef void (*draw_fn)(void *, const void *);
#define FILL_RECT   ((fill_fn)0x0808EA23u)     /* (rect, palette index, canvas) */
#define OUTLINE     ((fill_fn)0x0808E995u)
#define BTN_DRAW    ((draw_fn)0x080A3B6Du)     /* button vt+0x04 */
#define PAL_BG      2          /* 0x141414 */
#define PAL_WHITE   3
#define PAL_PINK    4          /* 0xFF6AF3: the project */
#define PAL_CYAN    5          /* 0x09D7F5: the loop */
#define PAL_DEAD    25         /* 0x2B2B2B: the ends, one side silent */
#define PAL_TRACK   17         /* 0x707070 */
#define PAL_EDGE    1          /* 0x56565A */
#define XF_MARGIN   12
#define XF_KNOB     16

static void rect(int *r, int x, int y, int w, int h)
{
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
}

/* Button vt+0x04 for the crossfader. Without an audio loop it is the stock button with the capture
 * hint. With one: a track (dark at both ends, where one side is silent), the knob, and the loop's
 * (cyan, left) and the project's (pink, right) level as bars at the edges. y runs bottom-up. */
static void xf_draw(void *w, const void *ctx)
{
    void *cv = *(void *const *)((const char *)ctx + 4);
    int x = (int)u32(w, 4), y = (int)u32(w, 8), wd = (int)u32(w, 0xC), h = (int)u32(w, 0x10);
    int r[4], tx = x + XF_MARGIN, tw = wd - 2 * XF_MARGIN, dz = tw * 15 / 100, pos, gl, gr;
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

static const void *const xf_vt[16] = {
    (const void *)0x080A3D03u, (const void *)xf_draw,    (const void *)0x080AEB1Du,
    (const void *)0x080AEBABu,
    (const void *)xf_touch,    /* +0x10 touchDown */
    (const void *)xf_touch,    /* +0x14 touchMove */
    (const void *)xf_up,       /* +0x18 touchUp */
    (const void *)0x080A3323u, (const void *)0x080A3759u, (const void *)0x080A3317u,
    (const void *)0x080AEB5Fu, (const void *)0x080A331Bu, (const void *)0x080AEC05u,
    (const void *)0x080A3321u, (const void *)0x080A3773u, (const void *)0x080A3979u,
};

static void build(void *song)
{
    static const int x[NBTN] = {0, 100, 200}, w[NBTN] = {99, 99, 120};
    unsigned i;
    void *xf = X->btn;
    BTN_CTOR(xf);
    *(const void *const **)xf = xf_vt;
    ((set_id_fn)xf_vt[9])(xf, XF_ID);
    BTN_LATCH(xf, 1);
    add_oldest(song, xf);
    set_rect(xf, 0, 40, 320, 48);
    HIDE(xf, 1);
    X->bar[0] = 0;
    X->drawn = -1;
    X->tops = 0;
    for (i = 0; i < NBTN; i++) {
        void *b = D->btn[i];
        BTN_CTOR(b);
        ((set_id_fn)(*(unsigned **)b)[9])(b, ID_DJ + i);
        BTN_LATCH(b, 1);
        add_oldest(song, b);
        set_rect(b, x[i], 0, w[i], 38);
        HIDE(b, 1);
    }
    D->ui.song = song;
    D->ui.lbl_jump = D->ui.lbl_loop = 0xFF;
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

/* Filter knob as a label: how far from the centre, either way: "Loop 45", "Proj Off". */
static void filt_text(char *d, const char *side, int v)
{
    char n[8];
    int a = v < 0 ? -v : v, k = 0, q = (a + 5) / 10;
    if (a <= 8) {
        cat(d, side, s_off_f);
        return;
    }
    if (q >= 100)
        n[k++] = '1', q -= 100, n[k++] = (char)('0' + q / 10), n[k++] = (char)('0' + q % 10);
    else if (q >= 10)
        n[k++] = (char)('0' + q / 10), n[k++] = (char)('0' + q % 10);
    else
        n[k++] = (char)('0' + q);
    n[k] = 0;
    cat(d, side, n);
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Our text on a stock button, again only when it shows something else (stock relabels BPM / Swing
 * on tempo changes). */
static void keep_text(void *btn, const char *t)
{
    void *lbl = (void *)u32(btn, BTN_LABEL);
    if (!u32(btn, 0))                /* not built (yet) */
        return;
    if (!lbl || !same((const char *)at(lbl, 0x65), t))
        BTN_TEXT(btn, t);
}

static void mixer_labels(void *song)
{
    int st = mx_state();
    char *b = X->bar;
    filt_text(X->top[0], s_loopside, mx_filter(1));   /* top-left: the loop, as the crossfader */
    filt_text(X->top[1], s_proj, mx_filter(0));
    keep_text(at(song, S_BTN_BPM), X->top[0]);
    keep_text(at(song, S_BTN_SWING), X->top[1]);
    keep_text(at(song, S_BTN_EDIT), s_scene_up);
    keep_text(at(song, S_BTN_LMODE), s_scene_dn);
    if (u32(at(song, S_BTN_LMODE), 0) && u8(at(song, S_BTN_LMODE), 0x96))
        BTN_STATE(at(song, S_BTN_LMODE), 0);
    X->tops = 1;
    if (st != 2) {                   /* the capture hint: BACK only once the pass is recorded */
        keep_text(X->btn, st ? s_recording : !D->ui.loop_on ? s_loop_first
                  : !mx_have() ? s_no_ring : mx_ready(D->loop_a, D->loop_len) ? s_capture : s_recording);
        return;
    }
    b[0] = 0;
    keep_text(X->btn, b);            /* xf_draw paints it; no text */
    if (X->drawn != mx_xf()) {       /* moved: paint again in both passes */
        *(unsigned char *)at(X->btn, 0x31) = 1;
        *(unsigned char *)at(X->btn, 0x32) = 1;
    }
}

static void labels(void)
{
    DjUi *ui = &D->ui;
    int audio = mx_state() != 0;
    unsigned char want = (unsigned char)(audio ? size_idx(mx_len()) | 0x80 : ui->loop_idx);
    if (ui->lbl_jump != ui->jump_idx) {
        cat(ui->text[0], s_back, jump_n[ui->jump_idx]);
        cat(ui->text[1], jump_n[ui->jump_idx], s_fwd);
        BTN_TEXT(D->btn[0], ui->text[0]);
        BTN_TEXT(D->btn[1], ui->text[1]);
        ui->lbl_jump = ui->jump_idx;
    }
    if (ui->lbl_loop != want) {
        cat(ui->text[2], audio ? s_audio : s_loop, loop_n[want & 0x7F]);
        BTN_TEXT(D->btn[2], ui->text[2]);
        ui->lbl_loop = want;
    }
    BTN_STATE(D->btn[0], 0);   /* a press may have lit them */
    BTN_STATE(D->btn[1], 0);
    BTN_STATE(D->btn[2], ui->loop_on || audio);
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

/* A refused jump: the scene it would have reached blinks three times. */
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

static void ui_update(void *song)
{
    DjUi *ui = &D->ui;
    unsigned show, i;
    if (ui->song != song)
        build(song);
    show = ui->want && u8(song, S_MODE) <= 1 && !u8(song, S_BAR_HID);
    if (show != ui->shown) {
        for (i = 0; i < NBTN; i++)
            HIDE(D->btn[i], !show);
        HIDE(X->btn, !show);
        if (!show)
            rows_dirty(song);    /* the list under the row paints again */
        ui->shown = (unsigned char)show;
    }
    if (show) {
        labels();
        mixer_labels(song);
    } else if (X->tops) {
        unsigned ev[6], j;
        for (j = 0; j < 6; j++)
            ev[j] = 0;
        ev[0] = EV_REFRESH;
        X->tops = 0;
        SONG_EVENT(song, ev);    /* BPM and Swing labelled again by stock */
        j = u32(song, S_LMODE);
        keep_text(at(song, S_BTN_EDIT), EDIT_TEXT);
        keep_text(at(song, S_BTN_LMODE), LMODE_TEXT[j < 3 ? j : 0]);
        if (u32(at(song, S_BTN_LMODE), 0))
            BTN_STATE(at(song, S_BTN_LMODE), j != 0);
    }
    led(show);
    blink(song);
}

/* App_HandleInput, SONG (0x080A329E): on the Song screen it shows / hides the row instead. */
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

/* Song vtable +0x30: our buttons act on the press (as a DJ deck), the release is swallowed. */
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

unsigned dj_song_child(void *song, const void *ev)
{
    unsigned type = u16(ev, 0), id = u32(ev, 0xC);
    if (live() && D->ui.shown && !u8(song, S_MODE) && (type == 1 || type == 2) &&
        (id == ID_EDIT || id == ID_LMODE)) {
        if (type == 2)                   /* on the release, as stock's Edit */
            scene_step(song, id == ID_EDIT ? -1 : 1);
        return 0;
    }
    if (!live() || (type != 1 && type != 2) || id - ID_DJ >= NBTN)
        return SONG_CHILD(song, ev);
    if (type == 1) {
        if (id == ID_DJ)
            request_jump(-1);
        else if (id == ID_DJ + 1)
            request_jump(1);
        else if (mx_state()) {
            mx_clear();          /* Loop off ends the audio loop */
        } else {
            D->ui.loop_on ^= 1;
            request_loop();
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

/* BACK with the row shown and the DJ loop on, once the ring has recorded the pass playing now:
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
    mx_set_filter(0, 0);                 /* a new loop starts with both filters off */
    mx_set_filter(1, 0);
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

/* Song vtable +0x34: with the row shown, the bottom knobs set the sizes (x2 / /2 per step). Leaving
 * the screen (0xD3) hides the row. */
void dj_song_event(void *song, const void *ev)
{
    unsigned type = u16(ev, 0);
    if (live() && type == 0xD3)
        D->ui.want = 0;
    if (live() && type == EV_BACK && try_capture())
        return;
    if (live() && type == 0x32 && D->ui.shown) {
        unsigned k = u16(ev, 0xC);
        int n;
        if (k == 0 || k == 2) {          /* top knobs: loop filter left, project filter right */
            unsigned side = k == 0;      /* mixer side: 0 project, 1 loop */
            int *acc = &X->facc[side];
            *acc += (short)u16(ev, 0x10);
            n = *acc / FILT_STEP;
            *acc -= n * FILT_STEP;
            mx_set_filter(side, mx_filter(side) + n);
            return;
        }
        if (k == 1 && mx_state() == 2) {  /* audio loop playing: bottom-left is the crossfader */
            int *acc = &D->ui.acc[0];
            *acc += (short)u16(ev, 0x10);
            n = *acc / FILT_STEP;
            *acc -= n * FILT_STEP;
            mx_set_xf(mx_xf_target() + n);
            return;
        }
        if (k == 1 || k == 3) {
            n = steps_of(k == 3, (short)u16(ev, 0x10));
            if (k == 1) {
                D->ui.jump_idx = step_idx(D->ui.jump_idx, n, JUMP_SIZES);
            } else {
                unsigned char was = D->ui.loop_idx;
                if (mx_state())
                    was = (unsigned char)size_idx(mx_len());
                D->ui.loop_idx = step_idx(was, n, LOOP_SIZES);
                if (mx_state() && D->ui.loop_idx != was)
                    mx_size(loop_t[D->ui.loop_idx]);   /* the audio loop takes it */
                else if (D->ui.loop_on && D->ui.loop_idx != was)
                    request_loop();      /* the live loop takes the new size */
            }
            return;
        }
    }
    SONG_EVENT(song, ev);
}
