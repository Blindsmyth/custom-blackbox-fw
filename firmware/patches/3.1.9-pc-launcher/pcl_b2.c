/* Program Change launcher (3.1.U), bank 2 side: the layer on the Presets screen. Reached through RbExt.pcl_ui from
 * pcl.c's stubs once bank 2 is paired (UI task). Plain C: no RAM of its own, no library calls (b2_plain_check).
 * The widget helpers follow dj.c's (place, add_oldest, keep_text, set_lit, ask_passes). docs/pc-launcher.md.
 *
 * Layout (y bottom-up, stock's header 222-240 stays): 2x2 as the encoders, each button 159 x 110:
 *   TL encoder: slot 1 (0, 111)    TR encoder: slot 3 (160, 111)
 *   BL encoder: slot 2 (0, 0)      BR encoder: slot 4 (160, 0)
 * The encoder index (event 0x32 +0xC: 0 TL, 1 BL, 2 TR, 3 BR) is the slot. */
#include <stdint.h>

#include "pcl.h"

typedef void (*view_fn)(void *);
typedef void (*rect_fn)(void *, const int *);
typedef void (*text_fn)(void *, const char *);
typedef void (*state_fn)(void *, unsigned);
typedef void (*set_id_fn)(void *, unsigned);
typedef int (*get_fn)(void *, unsigned);
typedef void (*set_fn)(void *, unsigned, int);
typedef void (*save_fn)(void *, const char *, void *);
#define BTN_CTOR     ((view_fn)0x080A3D35u)       /* stock frame button (0xA0 bytes) */
#define BTN_RECT     ((rect_fn)0x080A3759u)       /* x, y, w, h */
#define BTN_TEXT     ((text_fn)0x080A3DEDu)
#define BTN_STATE    ((state_fn)0x080A3E01u)      /* lit */
#define BTN_LATCH    ((state_fn)0x080A3773u)      /* draws in the partial pass */
#define HIDE         ((state_fn)0x080C4E61u)      /* hidden flag (+0x30) + redraw */
#define PARAM_GET    ((get_fn)0x08093E9Du)
#define PARAM_SET    ((set_fn)0x08093ECDu)
#define SAVE_SETTINGS ((save_fn)0x08090CBDu)      /* PresetMgr_RequestSaveSettings(pm, name, bag) */
#define PM_OBJ       ((void *)0x2401F508u)
#define S_SETTINGS   ((const char *)0x080CF300u)
#define GLOBAL_BAG   ((void *)0x24028D18u)        /* App+0x8C90: settings.tml */
#define INFO_LEVEL   (*(volatile const uint8_t *)0x24009BDEu)   /* INFO debounced: 0 = held */
#define UI_TICK      (*(volatile const uint32_t *)0x2402EEE0u)  /* ms */
#define W_HIDDEN     0x30
#define BTN_TEXT_AT  0x65         /* a button's label text (its label at +0x34, text at label +0x31) */
#define STEP_COUNTS  400          /* encoder counts per step, as stock's list knobs */
#define LIT_MS       150
#define EV_LEAVE     0xD3         /* the screen goes */
#define EV_KNOB      0x32

static unsigned u8(const void *p, unsigned off) { return ((const unsigned char *)p)[off]; }
static unsigned u16(const void *p, unsigned off) { return *(const uint16_t *)((const char *)p + off); }
static unsigned u32(const void *p, unsigned off) { return *(const unsigned *)((const char *)p + off); }
static void *at(void *p, unsigned off) { return (char *)p + off; }

static PclBlk *blk(void) { return (PclBlk *)PCL->blk; }

static void set_rect(void *b, int x, int y, int w, int h)
{
    int r[4];
    r[0] = x;
    r[1] = y;
    r[2] = w;
    r[3] = h;
    BTN_RECT(b, r);
}

/* As the screen's oldest child: drawn last (over the list) and hit first. */
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

static void dirty(PclBlk *d, void *w)
{
    *(unsigned char *)at(w, 0x31) = 1;
    *(unsigned char *)at(w, 0x32) = 1;
    d->ask |= 1;
}

static int ours(PclBlk *d, const void *w)
{
    return (const char *)w >= (const char *)d->btn && (const char *)w < (const char *)d->btn[PCL_N];
}

static void build(PclBlk *d, void *screen)
{
    static const int x[PCL_N] = {0, 0, 160, 160}, y[PCL_N] = {111, 0, 111, 0};
    unsigned i;
    for (i = 0; i < PCL_N; i++) {
        void *b = d->btn[i];
        BTN_CTOR(b);
        ((set_id_fn)(*(unsigned **)b)[9])(b, PCL_BTN_ID + i);
        BTN_LATCH(b, 1);
        add_oldest(screen, b);
        set_rect(b, x[i], y[i], 159, 110);
        HIDE(b, 1);
    }
    d->screen = screen;
}

/* Stock's widgets (the list, its slider, Load / Save / File / ^) go while the layer is up, and come back as they
 * were. */
static void stock_hide(PclBlk *d, void *screen, unsigned hide)
{
    unsigned i;
    if (hide) {
        void *c = (void *)u32(screen, 0x18);
        d->nkid = 0;
        for (; c && d->nkid < PCL_KIDS; c = (void *)u32(c, 0x20)) {
            if (ours(d, c))
                continue;
            d->kid[d->nkid] = c;
            d->kid_hid[d->nkid++] = (uint8_t)u8(c, W_HIDDEN);
            HIDE(c, 1);
        }
    } else {
        for (i = 0; i < d->nkid; i++) {
            HIDE(d->kid[i], d->kid_hid[i]);
            dirty(d, d->kid[i]);
        }
        d->nkid = 0;
    }
    d->ask |= 3;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* BTN_TEXT lays the label out again and repaints it: only when the text is another. */
static void keep_text(PclBlk *d, void *b, const char *t)
{
    if (!same((const char *)at(b, BTN_TEXT_AT), t)) {
        BTN_TEXT(b, t);
        dirty(d, b);
        d->ask |= 3;
    }
}

static void set_lit(PclBlk *d, void *b, unsigned on)
{
    if (u8(b, 0x96) != on) {
        BTN_STATE(b, on);
        d->ask |= 1;
    }
}

static char *num(char *o, unsigned v)
{
    if (v >= 100)
        *o++ = (char)('0' + v / 100);
    if (v >= 10)
        *o++ = (char)('0' + v / 10 % 10);
    *o++ = (char)('0' + v % 10);
    return o;
}

static char *str(char *o, const char *s)
{
    while (*s)
        *o++ = *s++;
    return o;
}

static unsigned slot_pgm(unsigned v) { unsigned p = v & 0xFF; return p < 1 ? 1 : p > 128 ? 128 : p; }
static unsigned slot_ch(unsigned v) { return ((v >> 8) & 15) + 1; }

/* "[Pgm 12]  Ch 3" while the encoders turn the program, "Pgm 12  [Ch 3]" after INFO. */
static void label(PclBlk *d, unsigned i)
{
    char t[24], *o = t;
    unsigned v = (unsigned)PARAM_GET(GLOBAL_BAG, PCL_ID + i), m = PCL->mode;
    o = str(o, m ? "Pgm " : "[Pgm ");
    o = num(o, slot_pgm(v));
    o = str(o, m ? "  [Ch " : "]  Ch ");
    o = num(o, slot_ch(v));
    if (m)
        *o++ = ']';
    *o = 0;
    keep_text(d, d->btn[i], t);
}

static void show(PclBlk *d, void *screen, unsigned on)
{
    volatile PclState *s = PCL;
    unsigned i;
    stock_hide(d, screen, on);
    for (i = 0; i < PCL_N; i++) {
        HIDE(d->btn[i], !on);
        if (on)
            dirty(d, d->btn[i]);
    }
    s->shown = (uint8_t)on;
    if (!on && s->changed) {           /* the card once per visit, not per encoder step */
        s->changed = 0;
        SAVE_SETTINGS(PM_OBJ, S_SETTINGS, GLOBAL_BAG);
    }
    if (on)
        s->info_prev = INFO_LEVEL;
}

/* Presets vtable +0x00, after stock's tick. */
void pcl_tick_b2(void *screen, void *flags)
{
    volatile PclState *s = PCL;
    PclBlk *d = blk();
    unsigned i, now = UI_TICK, lvl;
    if (d->screen != screen)
        build(d, screen);
    if (s->want != s->shown)
        show(d, screen, s->want);
    if (s->shown) {
        for (i = 0; i < d->nkid; i++)
            if (!u8(d->kid[i], W_HIDDEN)) {   /* stock showed it again (its ^ follows the browse path) */
                d->kid_hid[i] = 0;
                HIDE(d->kid[i], 1);
                d->ask |= 3;
            }
        lvl = INFO_LEVEL;
        if (s->info_prev && !lvl)
            s->mode ^= 1;                /* INFO pressed: the encoders change what they edit */
        s->info_prev = (uint8_t)lvl;
        for (i = 0; i < PCL_N; i++) {
            label(d, i);
            if (d->lit_ms[i] && (int)(now - d->lit_ms[i]) >= 0) {
                d->lit_ms[i] = 0;
                set_lit(d, d->btn[i], 0);
            }
        }
    }
    if (d->ask && flags) {
        if (d->ask & 2)
            ((unsigned char *)flags)[0] = 1;
        ((unsigned char *)flags)[1] = 1;
        d->ask = 0;
    }
}

/* Presets vtable +0x30: a press on one of ours sends its program change (pcl_pump, audio task). 1 = taken. */
int pcl_child_b2(void *screen, const void *ev)
{
    volatile PclState *s = PCL;
    PclBlk *d = blk();
    unsigned type = u16(ev, 0), id = u32(ev, 0xC) - PCL_BTN_ID, v;
    (void)screen;
    if (!s->shown || id >= PCL_N || (type != 1 && type != 2))
        return 0;
    if (type == 1) {
        v = (unsigned)PARAM_GET(GLOBAL_BAG, PCL_ID + id);
        s->ch = (uint8_t)(slot_ch(v) - 1);
        s->pgm = (uint8_t)(slot_pgm(v) - 1);
        __asm__ volatile("dmb" ::: "memory");
        s->seq++;
        set_lit(d, d->btn[id], 1);
        d->lit_ms[id] = UI_TICK + LIT_MS;
        if (!d->lit_ms[id])
            d->lit_ms[id] = 1;
    }
    return 1;
}

/* Presets vtable +0x34: the encoders while the layer is up, and closing it when the screen goes. 1 = taken. */
int pcl_event_b2(void *screen, const void *ev)
{
    volatile PclState *s = PCL;
    PclBlk *d = blk();
    unsigned type = u16(ev, 0), k, v, p, c;
    int n;
    if (type == EV_LEAVE && s->shown) {
        show(d, screen, 0);              /* want is kept: back on Presets, the layer is up again */
        return 0;                        /* stock's leave handling too */
    }
    if (type != EV_KNOB || !s->shown)
        return 0;
    k = u16(ev, 0xC);
    if (k >= PCL_N)
        return 1;
    n = d->acc[k] + (short)u16(ev, 0x10);
    d->acc[k] = (int16_t)(n % STEP_COUNTS);
    n /= STEP_COUNTS;
    if (!n)
        return 1;
    v = (unsigned)PARAM_GET(GLOBAL_BAG, PCL_ID + k);
    p = slot_pgm(v);
    c = slot_ch(v);
    if (s->mode) {
        n += (int)c;
        c = n < 1 ? 1u : n > 16 ? 16u : (unsigned)n;
    } else {
        n += (int)p;
        p = n < 1 ? 1u : n > 128 ? 128u : (unsigned)n;
    }
    if (PCL_VAL(p, c) != v) {
        PARAM_SET(GLOBAL_BAG, PCL_ID + k, (int)PCL_VAL(p, c));
        s->changed = 1;
    }
    return 1;
}

const uint32_t pcl_ui_b2[PCL_UI_N] = {(uint32_t)pcl_tick_b2, (uint32_t)pcl_child_b2, (uint32_t)pcl_event_b2};
