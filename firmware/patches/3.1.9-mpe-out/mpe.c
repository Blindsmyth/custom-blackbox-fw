/* Seq MIDI out: MPE channel spread and output port per sequence (3.1.U). Design and the stock code it
 * uses: docs/mpe-out.md.
 *
 * Every note a sequence sends out goes through one engine message (0x50 on, 0x51 off) whose byte +8
 * is the player's +0x68 (stock's dormant `midiport`) and +0xC its +0x69 (the MIDI Out channel).
 * Stock posts them from six places (the step's notes, their offs, all-off, the layer switch, live
 * notes and their all-off) and sends them all from one call in the audio task (Pcm_PollMidi), `bl FUN_08044A0E` at
 * 0x0804D004. So:
 *   - the audio task keeps player+0x68 = a tag: which of the 16 sequence players it is, and the
 *     sequence's Port and MPE mode (mpe_param, from the player's own param switch);
 *   - the send (audio task, Pcm_PollMidi) reads the tag back from the message and picks the channel
 *     (mpe_out): least recently used member channel, the oldest note stolen when all are busy, and
 *     a note-off on the channel its note-on went out on. Off is stock's bytes.
 * A tag change (Port or mode) runs stock's own all-off first, as stock does when the channel or
 * port changes, so every held note gets its off with the tag it went out under.
 *
 * Params (per sequence, in its bag with MIDI Out, Seq page 0; App_SetParam keeps them in layer 0):
 *   0x74  "Port:"    Both / DIN / USB    (stock "MIDI Port:" 1..4, in no template, never read)
 *   0x1F0 "MPE:"     Off / Lower / Upper / Multi
 *   0x1F1 "Last Ch:" 1..16, default 16: the last note channel
 *   0x1F2 "PB Rng:"  0..96, default 48
 * As Ableton's MPE / Multi-channel settings, the first note channel is the sequence's own MIDI Out
 * channel (0x6B, the message's +0xC). Note channels are those from first to last, either way round,
 * without the zone's master (Lower 1, Upper 16).
 * Program change per layer (layer pages A-D), as an Ableton MIDI clip's Bank / Sub / Pgm:
 *   0x1EC "Bank:"    --- / 1..128   CC 0
 *   0x1ED "Sub:"     --- / 1..128   CC 32
 *   0x1EE "Pgm:"     --- / 1..128   program change
 * They go out when the sequence starts and when a layer takes over, right before its first note:
 * The send sees the player's own pass (0x4E, every pass) and layer (0x69) messages in the same
 * queue as its notes (mpe_msg), and sends a layer's set on the first pass after each Player_Start
 * (counted by mpe_player_start in backup SRAM, as there are stop paths that post no 0x4F) and when
 * another layer takes over. Channel: MIDI Out, or the MPE master (Lower 1, Upper 16).
 *   0x1EF "Loop:"    Off / On (default On), per sequence: Off plays one pass. mpe_pass sets the
 *                    player's stop request (+0x32) as the pass starts, so stock's own stop at the
 *                    loop end (0x4F, all-off) ends it there, as a quantised stop does.
 * Ids 0x1E5 (stock App_SetParam's own) and the Song DJ row's widgets (to 0x1E4) are taken.
 *
 * Where it runs: bank 1 has room for little more than the hooks, so this file holds the registration,
 * the templates, the start count and Loop, and stubs; the rest (the param switch's work, the channel
 * choice at the send, the MPE setup and the program change) is in bank 2 (mpe_b2.c, RbExt.mpe_*). The
 * stubs call it once the pairing check has found bank 2 intact (RbVoice.state PAIRED, READY, SLOW or
 * BROKEN: before the Warp engine starts, so a boot's preset load is seen). Without a paired bank 2
 * (no BBWARP.BIN, being written) everything is stock's: notes on MIDI Out's channel, both ports.
 */

#include "mpe.h"
#include "b2.h"

typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef void (*reg_num_fn)(void *, unsigned, int, const char *, int, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef void (*start_fn)(void *, int, int);
#define PARAM_ENUM   ((reg_enum_fn)0x0808C0D9u)   /* reg, id, title, labels, count, xml */
#define PARAM_NUMBER ((reg_num_fn)0x0808C12Du)    /* reg, id, type, title, min, max, xml */
#define PARAM_ADD    ((param_add_fn)0x08093EF7u)  /* bag, id, default */
#define PLAYER_START ((start_fn)0x0805CB65u)      /* Player_Start(player, transport, router) */

static const char t_port[] = "Port:";
static const char x_port[] = "midiport";
static const char s_both[] = "Both";
static const char s_din[] = "DIN";
static const char s_usb[] = "USB";
static const char *const port_names[PORTS] = {s_both, s_din, s_usb};
static const char t_mode[] = "MPE:";
static const char x_mode[] = "mpemode";
static const char s_off[] = "Off";
static const char s_lower[] = "Lower";
static const char s_upper[] = "Upper";
static const char s_multi[] = "Multi";
static const char *const mode_names[MODES] = {s_off, s_lower, s_upper, s_multi};
static const char t_last[] = "Last Ch:";
static const char x_last[] = "mpelast";
static const char t_bend[] = "PB Rng:";
static const char x_bend[] = "mpebend";
static const char t_loop[] = "Loop:";
static const char x_loop[] = "seqloop";
static const char s_on[] = "On";
static const char *const loop_names[2] = {s_off, s_on};
static const char t_pc[3][6] = {"Bank:", "Sub:", "Pgm:"};
static const char x_pc[3][7] = {"pcbank", "pcsub", "pcpgm"};

/* "---", "1" .. "128" into backup SRAM (MP_LABELS): built, not stored. */
static const char *const *pc_labels(void)
{
    const char **ptr = (const char **)MP_LABELS;
    char *t = (char *)MP_LABEL_TXT;
    unsigned i;
    ptr[0] = t;
    *t++ = '-';
    *t++ = '-';
    *t++ = '-';
    *t++ = 0;
    for (i = 1; i < PC_LABELS; i++) {
        ptr[i] = t;
        if (i >= 100)
            *t++ = (char)('0' + i / 100);
        if (i >= 10)
            *t++ = (char)('0' + i / 10 % 10);
        *t++ = (char)('0' + i % 10);
        *t++ = 0;
    }
    return ptr;
}

#define B2_FN(field) RB_EXT_FN(field)

/* Param_RegisterAll, in place of stock's `midiport` int registration (bl 0x0808CDA2). Boot: the
 * state starts empty, every layer at the defaults. */
void mpe_register(void *reg)
{
    unsigned *w = (unsigned *)MP_BASE, i, k;
    S->magic = 0;
    for (i = 0; i < sizeof(MpState) / 4; i++)
        w[i] = 0;
    for (i = 0; i < N_PL; i++) {
        MpPlayer *p = &S->pl[i];
        for (k = 0; k < 8; k++)
            p->order[k] = (unsigned char)(2 * k | (2 * k + 1) << 4);
        p->cfg = CFG(PORT_BOTH, MODE_OFF, LAST_DEF - 1, BEND_DEF);
        p->sent = PC_NONE << 4;
    }
    S->magic = MP_MAGIC;
    rb_bkp_enable();
    B->magic = 0;
    for (i = 0; i < N_PL; i++)
        B->starts[i] = B->seen[i] = 0;
    B->magic = MP_BKP_MAGIC;
    PARAM_ENUM(reg, ID_LOOP, t_loop, loop_names, 2, x_loop);
    PARAM_ENUM(reg, ID_PORT, t_port, port_names, PORTS, x_port);
    PARAM_ENUM(reg, ID_MODE, t_mode, mode_names, MODES, x_mode);
    PARAM_NUMBER(reg, ID_LAST, 1, t_last, 1, 16, x_last);
    PARAM_NUMBER(reg, ID_BEND, 1, t_bend, 0, BEND_MAX, x_bend);
    for (i = 0; i < 3; i++)
        PARAM_ENUM(reg, ID_BANK + i, t_pc[i], pc_labels(), PC_LABELS, x_pc[i]);
}

/* Param_GroupTemplate, the layer bag (case 0x48, layers B-D), at its add of 0x86 (bl 0x0809443C):
 * Bank / Sub / Pgm, all ---. */
void mpe_tpl_layer(void *bag, unsigned id, int def)
{
    unsigned i;
    PARAM_ADD(bag, id, def);
    for (i = 0; i < 3; i++)
        PARAM_ADD(bag, ID_BANK + i, 0);
}

/* Param_GroupTemplate, the sequence bag (case 0x47: layer A, and the layer push's id list), at its
 * add of 0x88 (bl 0x0809441C): the sequence's MIDI out settings, then layer A's program change. */
void mpe_tpl(void *bag, unsigned id, int def)
{
    PARAM_ADD(bag, ID_LOOP, 1);
    PARAM_ADD(bag, ID_PORT, PORT_BOTH);
    PARAM_ADD(bag, ID_MODE, MODE_OFF);
    PARAM_ADD(bag, ID_LAST, LAST_DEF);
    PARAM_ADD(bag, ID_BEND, BEND_DEF);
    mpe_tpl_layer(bag, id, def);
}

/* The player's param switch (0x0805EBCE, audio task): bank 2's mpe_b2_param. 0 = stock's switch. */
int mpe_param(unsigned char *pl, void *router, const unsigned *msg)
{
    mpe_param_fn f = (mpe_param_fn)B2_FN(mpe_param);
    return f && mp_live() ? f(pl, router, msg) : 0;
}

/* The send at 0x0804D004 (audio task): bank 2's mpe_b2_out, else stock's FUN_08044A0E. */
void mpe_out(void *mgr, unsigned char *pk, int din, int usb, const unsigned char *msg)
{
    mpe_out_fn f = (mpe_out_fn)B2_FN(mpe_out);
    if (f && mp_live())
        f(mgr, pk, din, usb, msg);
    else
        MIDI_SEND(mgr, pk, din, usb);
}

/* Pcm_PollMidi's dispatch (0x0804D018, audio task): bank 2's mpe_b2_msg (the program change). */
void mpe_msg(const unsigned char *msg, const unsigned char *pcm)
{
    mpe_msg_fn f = (mpe_msg_fn)B2_FN(mpe_msg);
    if (f && mp_live())
        f(msg, pcm);
}

/* In place of `bl Player_Start` in the player's event loop (0x0805E9D2, 0x0805EA1A, 0x0805ED04, 0x0805EEDC,
 * audio task): a launch, quantised or not, counts for the program change. */
void mpe_player_start(unsigned char *pl, int transport, int router)
{
    int idx;
    PLAYER_START(pl, transport, router);
    if (mp_live() && B->magic == MP_BKP_MAGIC && (idx = key_idx(*(unsigned *)(pl + PL_KEY))) >= 0)
        B->starts[idx]++;
}

/* Player_Tick, right after a pass starts (its 0x4E post, mpe_pass_thunk; audio task). Loop Off: the
 * stop request, so the pass ends the sequence. */
void mpe_pass(unsigned char *pl)
{
    int idx;
    if (mp_live() && (idx = key_idx(*(unsigned *)(pl + PL_KEY))) >= 0 && C_ONCE(S->pl[idx].cfg))
        pl[PL_STOP] = 1;
}

#ifdef WITH_USBDEV
/* The USB MIDI device (3.1.UD) is in bank 2 too (../3.1.9-usbdev/usbdev.c). */
typedef int (*midi_pack_fn)(void *manager, uint8_t *queue, uint8_t *out, unsigned max, unsigned *len);

/* diag.c cpu_wait, once per audio block. */
void usbdev_poll(void)
{
    void (*f)(void) = (void (*)(void))B2_FN(usb_poll);
    if (f)
        f();
}

/* bl 0x08044B08: the "to USB" queue to the computer first, then stock's packer for the USB-A jack. */
int usbdev_midi_out(void *manager, uint8_t *queue, uint8_t *out, unsigned max, unsigned *len)
{
    void (*f)(uint8_t *) = (void (*)(uint8_t *))B2_FN(usb_out);
    if (f)
        f(queue);
    return ((midi_pack_fn)0x08044897u)(manager, queue, out, max, len);
}
#endif
