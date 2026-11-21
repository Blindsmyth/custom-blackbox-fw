/* Song DJ screens, bank 2 side (3.1.U): the table RbExt.dj_ui points at, in djui.c's order. The entries are dj.c's
 * own, from its bank-2 copy (build_patch.py renames them *_b2 and makes the rest of that copy local). */
#include <stdint.h>

void dj_song_key_b2(void *, unsigned, unsigned, unsigned);
void dj_song_tick_b2(void *, void *, void *, void *);
unsigned dj_song_child_b2(void *, const void *);
void dj_song_event_b2(void *, const void *);
void dj_fx_tick_b2(void *, void *, void *, void *);
unsigned dj_fx_child_b2(void *, const void *);
void dj_fx_event_b2(void *, const void *);
void dj_tools_tick_b2(void *, void *, void *, void *);
unsigned dj_tools_child_b2(void *, const void *);
void dj_tools_event_b2(void *, const void *);

const uint32_t dj_ui_b2[10] = {
    (uint32_t)dj_song_key_b2, (uint32_t)dj_song_tick_b2, (uint32_t)dj_song_child_b2, (uint32_t)dj_song_event_b2,
    (uint32_t)dj_fx_tick_b2, (uint32_t)dj_fx_child_b2, (uint32_t)dj_fx_event_b2,
    (uint32_t)dj_tools_tick_b2, (uint32_t)dj_tools_child_b2, (uint32_t)dj_tools_event_b2,
};
