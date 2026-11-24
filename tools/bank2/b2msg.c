/* "Installing" message while the card loader writes bank 2 (3.1.U lineup).
 *
 * The first boot after a new BLACKBOX.BIN writes BBWARP.BIN into flash bank 2 and restarts (b2load.c). Before this,
 * nothing showed on screen while it wrote, and the restart looked like a crash. Now the screen task puts up stock's
 * message screen (screen 1, the one that says "Please insert / microSD card") with three lines of ours from its first
 * tick on such a boot, and keeps it up until the restart:
 *   - from the start: the pairing check (rb_pair, while the app is built, before the first tick) found no payload of
 *     this image in bank 2 (RB_ST_ABSENT) and the card is in, so the loader is about to look;
 *   - while the loader writes (RbVoice.writing, disk task).
 * If the loader then writes nothing (no BBWARP.BIN, another build's, two failed tries ...: the Warp list names it),
 * the message comes down and the screen the app last asked for comes back.
 *
 * Screen task only: App_SetScreen and ui_post_event touch the UI's own objects and its unlocked event ring, so the
 * disk task never calls them.
 *   b2msg_update: in place of `bl App_Update` in the screen task's tick (FUN_0808B654, 0x0808B664). After stock's
 *                 update: while an install is due or running and screen 1 is not up with our lines, App_SetScreen(app,
 *                 1) and our event, as stock does for its card message (App_SetScreen(1), then event 0xA6). The
 *                 screen the app sets meanwhile is kept (RbVoice.back) and set again when the message comes down.
 *   b2msg_event:  the message screen's onEvent (vtable slot 0x080F19D8, stock FUN_080C7024, which draws a fixed
 *                 pair of lines per event 0xA6-0xA9): our event draws our lines, every other goes to stock.
 */
#include <stdint.h>

#include "b2.h"

typedef void (*update_fn)(void *app);
typedef void (*screen_fn)(void *app, int screen, int a, int b);
typedef void (*post_fn)(void *queue, const void *event);
typedef void (*draw_fn)(void *label, const char *text);
typedef void (*event_fn)(void *screen, const uint16_t *event);
#define APP_UPDATE   ((update_fn)0x080A2259u)
#define SET_SCREEN   ((screen_fn)0x0809EAEDu)
#define UI_POST      ((post_fn)0x080B5759u)
#define DRAW_TEXT    ((draw_fn)0x080C3B99u)    /* ui_draw_cstring(label, text) */
#define MSG_EVENT    ((event_fn)0x080C7025u)   /* stock's message screen onEvent */
#define APP_SCREEN   0x8CA4                    /* app byte: the screen App_SetScreen set last */
#define APP_EVENTS   0x30                      /* app's UI event ring (ui_post_event) */
#define SCREEN_MSG   1
#define SCREEN_MAIN  2                         /* the pads: where a boot ends */
#define CARD_FOUND   (*(const volatile uint8_t *)(0x240021a4u + 0x15u))   /* stock: the card's root opened */
#define EV_B2MSG     0x07E1                    /* ours: >= 0xCE and not 0x158 / 0x15B, so the tick forwards it */

static const char line1[] = "Installing Warp code";
static const char line2[] = "Do not power off";
static const char line3[] = "Restarts by itself";

void b2msg_update(uint8_t *app)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    uint16_t ev[12];
    unsigned i;
    unsigned st, cur, due;
    APP_UPDATE(app);
    if (v->magic != RB_VOICE_MAGIC)
        return;
    st = v->state;
    cur = app[APP_SCREEN];
    due = v->writing || ((st & 0xffu) == RB_ST_ABSENT && !(st & RB_ST_LOOKED) && CARD_FOUND == 1u);
    if (!due) {
        if (v->shown) {
            v->shown = 0;
            if (cur == SCREEN_MSG)
                SET_SCREEN(app, v->back ? v->back : SCREEN_MAIN, 0, 0);
        }
        return;
    }
    if (cur != SCREEN_MSG)
        v->back = (uint8_t)cur;           /* what the app wants once the message is down */
    if (v->shown && cur == SCREEN_MSG)
        return;
    SET_SCREEN(app, SCREEN_MSG, 0, 0);
    for (i = 0; i < 12; i++)
        ev[i] = 0;
    ev[0] = EV_B2MSG;
    UI_POST(app + APP_EVENTS, ev);
    v->shown = 1;
}

void b2msg_event(uint8_t *screen, const uint16_t *event)
{
    if (*event == EV_B2MSG) {
        DRAW_TEXT(screen + 0xE4, line1);
        DRAW_TEXT(screen + 0x13C, line2);
        DRAW_TEXT(screen + 0x194, line3);
        return;
    }
    MSG_EVENT(screen, event);
}
