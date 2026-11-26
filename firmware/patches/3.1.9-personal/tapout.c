/* Personal (WITH_PERSONAL, left out by build_patch.py --release): features for one setup, not for a release.
 *
 * Tap Out (Tools -> Clock, under BPM): CC 28 on DIN, locked to the clock, at a note value.
 *   Off, then the Delay's own list (param 0x33, its labels): 1/64, 1/32, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4,
 *   1/2T, 1/2, 1 bar. Global (settings.tml "tapout"), default Off.
 *   Tap Ch (under it): the tap's MIDI channel, 1-16, global ("tapch"), default 1.
 *   Counted from the engine's clock ticks (message 0x56, 24 PPQ) as Pcm_PollMidi pops them (audio task, through
 *   mpe_msg's hook at 0x0804D018). Start (0x57) is bar 1, beat 1: from there the position in the bar (4/4, 96 ticks)
 *   is counted, also while Tap Out is Off, and each value taps on its own grid in the bar, so it stays on the beats and
 *   bars and a change lands on the grid at once. Straight and triplet values divide the bar. The dotted ones (1/16D,
 *   1/8D) don't: switched to (or from Start) they wait for the next downbeat, then run on freely across the bars.
 *   A tap is a press and a release, as a footswitch: CC = TAP_VAL on the note value, CC = 0 half a note value
 *   later (1/16: high on the 16ths, low on the 32nds between). Stop, or Tap Out turned Off, sends the 0 too.
 *   The ticks are counted whatever Tools -> Clock's MIDI clock send is: that only picks where F8 goes.
 */
#include <stdint.h>

#include "b2.h"
#include "personal.h"

typedef void (*reg_num_fn)(void *, unsigned, int, const char *, int, int, const char *);
typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef int (*get_fn)(void *, unsigned);
typedef void (*send_fn)(void *, const unsigned char *, int, int);
#define PARAM_ENUM  ((reg_enum_fn)0x0808C0D9u)    /* reg, id, title, labels, count, xml */
#define PARAM_NUMBER ((reg_num_fn)0x0808C12Du)   /* reg, id, type, title, min, max, xml */
#define PARAM_ADD   ((param_add_fn)0x08093EF7u)   /* bag, id, default */
#define PARAM_GET   ((get_fn)0x08093E9Du)         /* bag, id: a plain scan, no lock */
#define GLOBAL_BAG  ((void *)0x24028D18u)
#define MIDI_SEND   ((send_fn)0x08044A0Fu)        /* (mgr, packet, DIN, USB): audio task only */
#define MIDI_MGR    ((void *)0x24002E88u)

#define MSG_TICK    0x56    /* engine -> MIDI queue: clock tick (F8), 24 PPQ */
#define MSG_START   0x57    /* Start (FA) */
#define MSG_STOP    0x58    /* Stop (FC) */

static const char t_tap[] = "Tap Out:";
static const char x_tap[] = "tapout";
static const char s_off[] = "Off";
static const char t_ch[] = "Tap Ch:";
static const char x_ch[] = "tapch";
/* Off, then the Delay time's labels (param 0x33, table 0x080ECD3C) */
static const char *const tap_names[TAP_N] = {
    s_off,
    (const char *)0x080CEA30u, (const char *)0x080CEA20u, (const char *)0x080CE714u,   /* 1/64 1/32 1/16 */
    (const char *)0x080CEA10u, (const char *)0x080CEAC4u, (const char *)0x080CE710u,   /* 1/8T 1/16D 1/8 */
    (const char *)0x080CEA08u, (const char *)0x080CEACCu, (const char *)0x080CE70Cu,   /* 1/4T 1/8D 1/4 */
    (const char *)0x080CEA00u, (const char *)0x080CE708u, (const char *)0x080CE700u,   /* 1/2T 1/2 1 bar */
};
/* half clock ticks (48 a beat) per tap, by setting; 0 = Off. Half ticks for 1/64 (1.5 ticks: 1, 2, 1, 2 ...) */
static const uint8_t tap_half[TAP_N] = {0, 3, 6, 12, 16, 18, 24, 32, 36, 48, 64, 96, 192};

void personal_register(void *reg)
{
    rb_bkp_enable();
    TAP->pos = 0;
    TAP->prev = 0xFF;
    TAP->wait = 0;
    TAP->dacc = 0;
    TAP->due = 1;
    TAP->high = 0;
    PARAM_ENUM(reg, ID_TAPOUT, t_tap, tap_names, TAP_N, x_tap);
    PARAM_NUMBER(reg, ID_TAPCH, 1, t_ch, 1, 16, x_ch);   /* type 1 as stock's MIDI channels (0x28) */
}

void personal_global_tpl(void *bag)
{
    PARAM_ADD(bag, ID_TAPOUT, 0);
    PARAM_ADD(bag, ID_TAPCH, 1);
}

/* CC TAP_CC = v on DIN, on Tap Ch (audio task) */
static void cc(unsigned v)
{
    unsigned ch = (unsigned)PARAM_GET(GLOBAL_BAG, ID_TAPCH);
    unsigned char pk[8] = {(unsigned char)(0xB0 | ((ch >= 1 && ch <= 16 ? ch : 1) - 1)), TAP_CC, (unsigned char)v, 0, 3};
    MIDI_SEND(MIDI_MGR, pk, 1, 0);
    TAP->high = v != 0;
}

#define BAR_HALF    192u    /* a 4/4 bar in half ticks */
#define DOTTED(s)   ((s) == 5u || (s) == 8u)   /* 1/16D, 1/8D: no grid in the bar */

/* Pcm_PollMidi's dispatch (audio task), every message popped from the MIDI queue. */
void personal_msg(const unsigned char *msg)
{
    volatile TapState *t = TAP;
    unsigned set, period, q, r;
    if (msg[0] == MSG_START) {           /* the next tick is bar 1, beat 1; a dotted value starts there */
        t->due = 1;
        t->prev = 0xFF;
        return;
    }
    if (msg[0] == MSG_STOP) {
        if (t->high)
            cc(0);
        return;
    }
    if (msg[0] != MSG_TICK)
        return;
    q = t->due ? 0u : (t->pos + 2u) % BAR_HALF;   /* this tick covers half ticks q and q + 1 of the bar */
    t->due = 0;
    t->pos = (uint8_t)q;
    set = (unsigned)PARAM_GET(GLOBAL_BAG, ID_TAPOUT);
    period = set < TAP_N ? tap_half[set] : 0;
    if (!period) {
        if (t->high)
            cc(0);
        return;
    }
    if (set != t->prev) {                /* switched: a dotted value starts on the next downbeat */
        t->prev = (uint8_t)set;
        t->wait = DOTTED(set);
    }
    if (DOTTED(set)) {
        if (t->wait) {
            if (q != 0) {
                if (t->high)
                    cc(0);
                return;
            }
            t->wait = 0;
            t->dacc = 0;
        } else {
            t->dacc = (uint8_t)((t->dacc + 2u) % period);
        }
        r = t->dacc;                     /* even: it falls on a tick */
    } else {
        r = q % period;                  /* a grid point in q or q + 1 (1/64: 1.5 ticks) taps */
    }
    if (r == 0 || r == period - 1) {
        if (t->high)                     /* still pressed (1/64): release first */
            cc(0);
        cc(TAP_VAL);
    } else if (t->high && 2u * r >= period) {
        cc(0);                           /* half a note value after the tap */
    }
}
