/* Master chain, bank 2 side: the UI's drawing (RbExt.ui, lanes.cpp). Called directly from the UI
 * task by bank 1 (song-dj/dj.c), never from the audio task; reads the meters and holds bank 2's
 * audio side leaves in SRAM4 (mc_fw.h). Draws with stock's primitives, as dj.c does. */
#include "mc_fw.h"

typedef void (*fill_fn)(const int *, unsigned, void *);
#define FILL_RECT   ((fill_fn)0x0808EA23u)     /* (rect {x, y, w, h}, palette index, canvas) */
#define OUTLINE     ((fill_fn)0x0808E995u)
#define PAL_BG      2      /* 0x141414 */
#define PAL_WHITE   3
#define PAL_PINK    4      /* 0xFF6AF3 */
#define PAL_EDGE    1      /* 0x56565A */
#define PAL_TICK    16     /* 0x444444 */
#define GR_RANGE    12.0f  /* dB across the bar */

static int u32(const void *p, unsigned off) { return *(const int *)((const char *)p + off); }

static void rect(int *r, int x, int y, int w, int h) { r[0] = x; r[1] = y; r[2] = w; r[3] = h; }

static float gr_of(const McShared *s, unsigned kind, float *hold)
{
    if (kind == MC_KIND_CMP) {
        *hold = s->cmp_hold;
        return s->m.cmp_gr_db;
    }
    *hold = s->max_hold;
    return s->p.chain_on > 0.5f ? s->m.max_gr_db : s->m.lim_gr_db;
}

static int px(float db, int w)
{
    float f = -db / GR_RANGE;
    f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
    return (int)(f * (float)w + 0.5f);
}

/* "-3.2" into p, tenths, returns the end */
static char *tenths(char *p, float v)
{
    int t = (int)(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
    if (t < 0) { *p++ = '-'; t = -t; }
    if (t >= 1000) t = 999;
    if (t >= 100) *p++ = (char)('0' + t / 100);
    *p++ = (char)('0' + t / 10 % 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    return p;
}

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

unsigned mc_b2_ui(unsigned op, unsigned a, unsigned b, unsigned c)
{
    const McShared *s = MC_SHARED;
    float hold, gr;
    if (s->magic != MC_SHARED_MAGIC)
        return 0;
    switch (op) {
    case MC_UI_METER: {
        const void *w = (const void *)a;
        void *cv = *(void *const *)((const char *)b + 4);
        int x = u32(w, 4), y = u32(w, 8), wd = u32(w, 0xC), h = u32(w, 0x10), r[4], i;
        gr = gr_of(s, c, &hold);
        rect(r, x, y, wd, h);
        FILL_RECT(r, PAL_BG, cv);
        rect(r, x, y, px(gr, wd), h);
        if (r[2] > 0)
            FILL_RECT(r, PAL_PINK, cv);
        for (i = 3; i < (int)GR_RANGE; i += 3) {          /* a tick every 3 dB */
            rect(r, x + px(-(float)i, wd), y, 1, h);
            FILL_RECT(r, PAL_TICK, cv);
        }
        if (hold < -0.05f) {
            rect(r, x + px(hold, wd) - 1, y, 2, h);       /* the deepest since BACK */
            FILL_RECT(r, PAL_WHITE, cv);
        }
        rect(r, x, y, wd, h);
        OUTLINE(r, PAL_EDGE, cv);
        return 1;
    }
    case MC_UI_TEXT: {
        char *p = (char *)a;
        gr = gr_of(s, b, &hold);
        p = put(p, "GR ");
        p = tenths(p, gr);
        p = put(p, "   MAX ");
        p = tenths(p, hold);
        *p = 0;
        return 1;
    }
    default:
        return 0;
    }
}
