/* Personal (WITH_PERSONAL, left out by build_patch.py --release): features for one setup, not for a release.
 *
 * Tap Out (Tools -> Clock, under BPM): CC 15 on DIN, locked to the clock, at a note value.
 *   Off, then the Delay's own list (param 0x33, its labels): 1/64, 1/32, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4,
 *   1/2T, 1/2, 1 bar. Global (settings.tml "tapout"), default Off.
 *   Counted from the engine's clock ticks (message 0x56, 24 PPQ) as Pcm_PollMidi pops them (audio task, through
 *   mpe_msg's hook at 0x0804D018); Start (0x57) puts the next tick on a tap, so taps sit on the beat.
 *   The ticks are counted whatever Tools -> Clock's MIDI clock send is: that only picks where F8 goes.
 */
#include <stdint.h>

#include "b2.h"
#include "personal.h"

typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef int (*get_fn)(void *, unsigned);
typedef void (*send_fn)(void *, const unsigned char *, int, int);
#define PARAM_ENUM  ((reg_enum_fn)0x0808C0D9u)    /* reg, id, title, labels, count, xml */
#define PARAM_ADD   ((param_add_fn)0x08093EF7u)   /* bag, id, default */
#define PARAM_GET   ((get_fn)0x08093E9Du)         /* bag, id: a plain scan, no lock */
#define GLOBAL_BAG  ((void *)0x24028D18u)
#define MIDI_SEND   ((send_fn)0x08044A0Fu)        /* (mgr, packet, DIN, USB): audio task only */
#define MIDI_MGR    ((void *)0x24002E88u)

#define MSG_TICK    0x56    /* engine -> MIDI queue: clock tick (F8), 24 PPQ */
#define MSG_START   0x57    /* Start (FA) */

static const char t_tap[] = "Tap Out:";
static const char x_tap[] = "tapout";
static const char s_off[] = "Off";
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
    TAP->count = 0;
    TAP->due = 1;
    PARAM_ENUM(reg, ID_TAPOUT, t_tap, tap_names, TAP_N, x_tap);
}

void personal_global_tpl(void *bag)
{
    PARAM_ADD(bag, ID_TAPOUT, 0);
}

/* Pcm_PollMidi's dispatch (audio task), every message popped from the MIDI queue. */
void personal_msg(const unsigned char *msg)
{
    volatile TapState *t = TAP;
    unsigned set, period, n;
    if (msg[0] == MSG_START) {           /* the next tick is a tap: on the beat */
        t->due = 1;
        return;
    }
    if (msg[0] != MSG_TICK)
        return;
    set = (unsigned)PARAM_GET(GLOBAL_BAG, ID_TAPOUT);
    period = set < TAP_N ? tap_half[set] : 0;
    if (!period) {
        t->due = 1;                      /* turned on later: taps at once */
        return;
    }
    n = t->count + 2u;                   /* half ticks since the last tap */
    if (t->due) {
        t->due = 0;
        n = period;
    }
    if (n >= period) {
        unsigned char pk[8] = {(unsigned char)(0xB0 | (TAP_CH - 1)), TAP_CC, TAP_VAL, 0, 3};
        MIDI_SEND(MIDI_MGR, pk, 1, 0);
        n -= period;
    }
    t->count = (uint8_t)n;
}
