/* Program Change launcher (3.1.U), bank 1 side. docs/pc-launcher.md.
 *
 * PSET on the Presets screen opens a second layer: four big buttons, 2x2 as the encoders sit. Each has a program
 * (1-128) and a MIDI channel (1-16); its encoder turns the program, or the channel after INFO. A tap sends that
 * button's program change, on DIN and USB as stock MIDI out does. The four are global (settings.tml).
 *
 * Bank 1 has the hooks and what must work without bank 2; the screen itself is in bank 2 (pcl_b2.c, RbExt.pcl_ui),
 * called once rb_pair has found bank 2 intact (rb_ext_word). Without it PSET and the Presets screen are stock's.
 *   pcl_pset_key  bl 0x080A32EA, App_HandleInput's PSET: App_SetScreen(app, 7) -> on the Presets screen, the layer.
 *   pcl_tick / pcl_child / pcl_event  Presets vtable +0x00 / +0x30 / +0x34 (the last in front of the Mix line's
 *                 pset_on_event).
 *   pcl_pump      bl 0x0804CED8, Pcm_PollMidi's MIDI out pump (audio task, once a block): the only task that may
 *                 call FUN_08044A0E. A press leaves its program change in a mailbox; this sends it.
 *   pcl_register / pcl_global_tpl  from mixer.c's mx_register / mx_global_tpl: the four params, global.
 */
#include <stdint.h>

#include "b2.h"
#include "pcl.h"

typedef void (*reg_num_fn)(void *, unsigned, int, const char *, int, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef void (*key_fn)(void *, unsigned, unsigned, unsigned);
typedef void (*tick_fn)(void *, void *, void *, void *);
typedef unsigned (*child_fn)(void *, const void *);
typedef void (*send_fn)(void *, const unsigned char *, int, int);
typedef void (*pump_fn)(void *);
typedef void *(*alloc_fn)(unsigned);
#define PARAM_NUMBER ((reg_num_fn)0x0808C12Du)    /* reg, id, type, title, min, max, xml */
#define PARAM_ADD    ((param_add_fn)0x08093EF7u)  /* bag, id, default */
#define SET_SCREEN   ((key_fn)0x0809EAEDu)        /* App_SetScreen */
#define PSET_TICK    ((tick_fn)0x080BE595u)       /* Presets vtable +0x00 */
#define PSET_CHILD   ((child_fn)0x080BD929u)      /* Presets vtable +0x30 */
#define MIDI_SEND    ((send_fn)0x08044A0Fu)       /* (mgr, packet, DIN, USB) */
#define MIDI_PUMP    ((pump_fn)0x08044AD9u)       /* FUN_08044AD8(mgr): packs the "to USB" queue, UARTs */
#define SDRAM_ALLOC  ((alloc_fn)0x0804436Du)      /* bump from the bottom of SDRAM, at boot only */
#define MIDI_DIN     (*(volatile const uint8_t *)0x2401504Bu)   /* pcm + 0xA68B: notes to DIN */
#define MIDI_USB     (*(volatile const uint8_t *)0x2401504Du)   /* pcm + 0xA68D: USB Dev Out */

void pset_on_event(void *screen, const void *ev);   /* folders-repitch-mix pset.S: stock, then the ^ label */

static const char t_pcl[] = "PC Launch:";
static const char x_pcl[PCL_N][5] = {"pcl1", "pcl2", "pcl3", "pcl4"};

/* Param_RegisterAll (mixer.c mx_register): hidden numbers, edited only by the launcher. Boot: the state starts
 * empty and the buttons get their memory (SDRAM, once). */
void pcl_register(void *reg)
{
    volatile PclState *s = PCL;
    unsigned i;
    rb_bkp_enable();
    s->magic = 0;
    s->blk = (uint32_t)SDRAM_ALLOC(sizeof(PclBlk));
    s->want = s->shown = s->mode = s->changed = 0;
    s->info_prev = 1;
    s->seq = s->sent = 0;
    if (s->blk) {
        uint8_t *b = (uint8_t *)s->blk;
        for (i = 0; i < sizeof(PclBlk); i++)
            b[i] = 0;
    }
    s->magic = PCL_MAGIC;
    for (i = 0; i < PCL_N; i++)
        PARAM_NUMBER(reg, PCL_ID + i, 1, t_pcl, 0, 0xFFF, x_pcl[i]);
}

/* The global bag's template (mixer.c mx_global_tpl): slot n = program n, channel 1. */
void pcl_global_tpl(void *bag)
{
    unsigned i;
    for (i = 0; i < PCL_N; i++)
        PARAM_ADD(bag, PCL_ID + i, PCL_VAL(i + 1, 1));
}

static uint32_t ui(unsigned i)
{
    const uint32_t *t = (const uint32_t *)RB_EXT_FN(pcl_ui);
    return t && pcl_live() ? t[i] : 0u;
}

void pcl_pset_key(uint8_t *app, unsigned screen, unsigned a, unsigned b)
{
    if (app[APP_SCREEN] == SCR_PSET && ui(PCL_TICK)) {
        PCL->want ^= 1;
        return;
    }
    SET_SCREEN(app, screen, a, b);
}

void pcl_tick(void *screen, void *flags, void *a, void *b)
{
    void (*f)(void *, void *) = (void (*)(void *, void *))ui(PCL_TICK);
    PSET_TICK(screen, flags, a, b);
    if (f)
        f(screen, flags);
}

unsigned pcl_child(void *screen, const void *ev)
{
    int (*f)(void *, const void *) = (int (*)(void *, const void *))ui(PCL_CHILD);
    if (f && f(screen, ev))
        return 0;
    return PSET_CHILD(screen, ev);
}

void pcl_event(void *screen, const void *ev)
{
    int (*f)(void *, const void *) = (int (*)(void *, const void *))ui(PCL_EVENT);
    if (f && f(screen, ev))
        return;
    pset_on_event(screen, ev);
}

/* Audio task, once a block: a press's program change goes out here, in the task that owns the MIDI queues. */
void pcl_pump(void *mgr)
{
    volatile PclState *s = PCL;
    if (pcl_live() && s->seq != s->sent) {
        unsigned char pk[8];
        __asm__ volatile("dmb" ::: "memory");
        pk[0] = (unsigned char)(0xC0 | (s->ch & 15));
        pk[1] = (unsigned char)(s->pgm & 0x7F);
        pk[2] = pk[3] = 0;
        pk[4] = 2;
        s->sent = s->seq;
        MIDI_SEND(mgr, pk, MIDI_DIN, MIDI_USB);
    }
    MIDI_PUMP(mgr);
}
