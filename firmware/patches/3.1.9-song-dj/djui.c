/* Song DJ screens, bank 1 side (3.1.U): the ten screen entries of dj.c run from bank 2 (bank 1 is full), and these
 * stubs are what the hook sites and vtable slots point at (sites.py). Once the pairing check has found bank 2
 * intact (rb_ext_word), each calls its bank-2 copy (dj.c built there, the entry renamed *_b2; djui_b2.c lists them,
 * RbExt.dj_ui). Without bank 2 (no BBWARP.BIN, the install boot) each runs stock's own handler: the Song, DJ FX and
 * Tools screens as on 3.1.9, without the DJ row, the deck, the master row or Song cues' pad grid. */
#include <stdint.h>

#include "b2.h"

enum { DJ_KEY, DJ_SONG_TICK, DJ_SONG_CHILD, DJ_SONG_EVENT, DJ_FX_TICK, DJ_FX_CHILD, DJ_FX_EVENT, DJ_TOOLS_TICK,
       DJ_TOOLS_CHILD, DJ_TOOLS_EVENT, DJ_UI_N };

typedef void (*key_fn)(void *, unsigned, unsigned, unsigned);
typedef void (*tick_fn)(void *, void *, void *, void *);
typedef unsigned (*child_fn)(void *, const void *);
typedef void (*event_fn)(void *, const void *);

/* stock's handlers these slots held (sites.py) */
#define SET_SCREEN   ((key_fn)0x0809EAEDu)
#define SONG_TICK    ((tick_fn)0x080C26FDu)
#define SONG_CHILD   ((child_fn)0x080C3541u)
#define SONG_EVENT   ((event_fn)0x080C3861u)
#define LIST_TICK    ((tick_fn)0x080AEB45u)    /* DJ FX and Tools */
#define FX_CHILD     ((child_fn)0x080B1EE5u)
#define FX_EVENT     ((event_fn)0x080B1E9Du)
#define TOOLS_CHILD  ((child_fn)0x080C5361u)
#define TOOLS_EVENT  ((event_fn)0x080C52E1u)

static uint32_t fn(unsigned i)
{
    const uint32_t *t = (const uint32_t *)RB_EXT_FN(dj_ui);
    return t ? t[i] : 0u;
}

void dj_song_key(void *app, unsigned screen, unsigned a, unsigned b)
{
    key_fn f = (key_fn)fn(DJ_KEY);
    (f ? f : SET_SCREEN)(app, screen, a, b);
}

void dj_song_tick(void *song, void *flags, void *a, void *b)
{
    tick_fn f = (tick_fn)fn(DJ_SONG_TICK);
    (f ? f : SONG_TICK)(song, flags, a, b);
}

unsigned dj_song_child(void *song, const void *ev)
{
    child_fn f = (child_fn)fn(DJ_SONG_CHILD);
    return (f ? f : SONG_CHILD)(song, ev);
}

void dj_song_event(void *song, const void *ev)
{
    event_fn f = (event_fn)fn(DJ_SONG_EVENT);
    (f ? f : SONG_EVENT)(song, ev);
}

void dj_fx_tick(void *scr, void *flags, void *a, void *b)
{
    tick_fn f = (tick_fn)fn(DJ_FX_TICK);
    (f ? f : LIST_TICK)(scr, flags, a, b);
}

unsigned dj_fx_child(void *scr, const void *ev)
{
    child_fn f = (child_fn)fn(DJ_FX_CHILD);
    return (f ? f : FX_CHILD)(scr, ev);
}

void dj_fx_event(void *scr, const void *ev)
{
    event_fn f = (event_fn)fn(DJ_FX_EVENT);
    (f ? f : FX_EVENT)(scr, ev);
}

void dj_tools_tick(void *tools, void *flags, void *a, void *b)
{
    tick_fn f = (tick_fn)fn(DJ_TOOLS_TICK);
    (f ? f : LIST_TICK)(tools, flags, a, b);
}

unsigned dj_tools_child(void *tools, const void *ev)
{
    child_fn f = (child_fn)fn(DJ_TOOLS_CHILD);
    return (f ? f : TOOLS_CHILD)(tools, ev);
}

void dj_tools_event(void *tools, const void *ev)
{
    event_fn f = (event_fn)fn(DJ_TOOLS_EVENT);
    (f ? f : TOOLS_EVENT)(tools, ev);
}
