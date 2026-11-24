/* Seq MIDI out (3.1.U), bank 2: the work behind bank 1's stubs (mpe.c), reached through RbExt.mpe_*
 * once bank 2 is paired. Plain functions on SRAM4 / backup SRAM state: no .data, no .bss, no library calls
 * (built -ffreestanding; build_patch.py checks for undefined symbols), so they run before the Warp engine
 * has booted (its ITCM copy and heap are not needed). docs/mpe-out.md. */

#include "mpe.h"

/* The player's param switch (0x0805EBCE, audio task), for every 0x39 message to this player:
 * msg +0xC id, +0x10 value, +0x14 layer. Returns 1 when stock's switch must not run (Port: stock's
 * case would store the raw value and compare it with the tag). Ours are layer 0's: a layer push
 * also posts them for layers 1-3 (its id list is the sequence template), and those are dropped.
 * Any post of them (an edit, a preset load) or of MIDI Out has the MPE setup sent again. */
int mpe_b2_param(unsigned char *pl, void *router, const unsigned *msg)
{
    int idx, handled = 1;
    unsigned id, old, now, tag, was;
    int value;
    MpPlayer *p;
    if (!mp_live() || (idx = key_idx(*(unsigned *)(pl + PL_KEY))) < 0)
        return 0;
    p = &S->pl[idx];
    id = msg[3] & 0xFFFF;
    value = (int)msg[4];
    old = now = p->cfg;
    if (id >= ID_BANK && id <= ID_PGM) {               /* per layer, that layer's (a push sends all four) */
        if ((msg[5] & 0xFFFF) < 4)
            p->pc[msg[5] & 3][id - ID_BANK] = (unsigned char)clamp(value, 0, 128);
        return 1;
    }
    if (id == ID_OUTCH)                                /* as stock's case: +0x44 = on, +0x69 = channel */
        p->outch = (unsigned char)clamp(value, 0, 16);
    if ((msg[5] & 0xFFFF) != 0 && (id == ID_PORT || (id >= ID_LOOP && id <= ID_BEND)))
        return 1;
    switch (id) {
    case ID_PORT:
        now = (now & ~3u) | clamp(value, 0, PORTS - 1);
        break;
    case ID_MODE:
        now = (now & ~(3u << 2)) | clamp(value, 0, MODES - 1) << 2;
        break;
    case ID_LAST:
        now = (now & ~(15u << 4)) | (clamp(value, 1, 16) - 1) << 4;
        break;
    case ID_BEND:
        now = (now & ~(127u << 8)) | clamp(value, 0, BEND_MAX) << 8;
        break;
    case ID_LOOP:
        now = (now & 0x7FFFu) | (value ? 0u : 0x8000u);
        break;
    default:
        handled = 0;
    }
    p->cfg = (unsigned short)now;
    if (((handled || id == ID_OUTCH) && id != ID_LOOP) || ((now ^ old) & 0x7FFFu))
        p->gen++;
    tag = TAG(idx, C_PORT(now), C_MODE(now));
    was = pl[PL_TAG];
    if (was != tag) {
        unsigned wp = PORT_BOTH, wm = MODE_OFF;
        if (was >= TAG0) {
            wp = ((was - TAG0) % 12) / 4;
            wm = (was - TAG0) % 4;
        }
        if (wp != C_PORT(now) || wm != C_MODE(now)) {
            ALL_OFF(pl, 0, router, 1);
            LIVE_OFF(pl, router);
        }
        pl[PL_TAG] = (unsigned char)tag;
    }
    return handled;
}

/* ---- the send (audio task, Pcm_PollMidi) at 0x0804D004 */

/* Note channels (0-based mask): first (MIDI Out) to last, either way round, without the master. */
static unsigned members(unsigned mode, unsigned first, unsigned last)
{
    unsigned lo = first < last ? first : last, hi = first < last ? last : first;
    unsigned m = ((2u << hi) - 1) & ~((1u << lo) - 1);
    if (mode == MODE_LOWER)
        m &= ~1u;
    else if (mode == MODE_UPPER)
        m &= ~0x8000u;
    else if (mode != MODE_MULTI)
        m = 0;
    return m;
}

static unsigned at(const MpPlayer *p, unsigned i)
{
    return (p->order[i >> 1] >> (4 * (i & 1))) & 15;
}

static void put(MpPlayer *p, unsigned i, unsigned ch)
{
    unsigned sh = 4 * (i & 1);
    p->order[i >> 1] = (unsigned char)((p->order[i >> 1] & ~(15u << sh)) | ch << sh);
}

/* Channel ch to the end of the order: the most recently used. */
static void touch(MpPlayer *p, unsigned ch)
{
    unsigned i, j;
    for (i = 0; i < 15 && at(p, i) != ch; i++)
        ;
    for (j = i; j < 15; j++)
        put(p, j, at(p, j + 1));
    put(p, 15, ch);
}

static void send3(void *mgr, unsigned st, unsigned d1, unsigned d2, int din, int usb)
{
    unsigned char pk[8];
    pk[0] = (unsigned char)st;
    pk[1] = (unsigned char)(d1 & 0x7F);
    pk[2] = (unsigned char)(d2 & 0x7F);
    pk[3] = 0;
    pk[4] = 3;
    MIDI_SEND(mgr, pk, din, usb);
}

static void rpn(void *mgr, unsigned ch, unsigned num, unsigned val, int din, int usb)
{
    send3(mgr, 0xB0 | ch, 101, 0, din, usb);
    send3(mgr, 0xB0 | ch, 100, num, din, usb);
    send3(mgr, 0xB0 | ch, 6, val, din, usb);
    send3(mgr, 0xB0 | ch, 38, 0, din, usb);
    send3(mgr, 0xB0 | ch, 101, 127, din, usb);
    send3(mgr, 0xB0 | ch, 100, 127, din, usb);
}

/* The MPE setup, before the first note after a change: MPE Configuration Message (RPN 6 = N) on the
 * zone's master channel, N reaching the last note channel, then the pitch bend range (RPN 0) on every
 * note channel and 2 on the master. Multichannel has no master: only the note channels' range. */
static void setup(void *mgr, unsigned mode, unsigned m, unsigned bend, int din, int usb)
{
    unsigned ch, lo = 15, hi = 0, master = mode == MODE_UPPER ? 15 : 0;
    for (ch = 0; ch < 16; ch++)
        if (m >> ch & 1) {
            lo = ch < lo ? ch : lo;
            hi = ch;
        }
    if (mode != MODE_MULTI)
        rpn(mgr, master, 6, mode == MODE_UPPER ? 15 - lo : hi, din, usb);
    for (ch = 0; ch < 16; ch++)
        if (m >> ch & 1)
            rpn(mgr, ch, 0, bend, din, usb);
    if (mode != MODE_MULTI)
        rpn(mgr, master, 0, 2, din, usb);
}

/* `bl FUN_08044A0E` at 0x0804D004 (audio task), for engine messages 0x50-0x53: (mgr, packet, DIN,
 * USB) as stock, plus the message (+0 type, +8 tag, +0xC MIDI Out channel, +0x10 note). */
void mpe_b2_out(void *mgr, unsigned char *pk, int din, int usb, const unsigned char *msg)
{
    unsigned tag = msg[8], q, port, mode, cfg, m, i, ch, note;
    MpPlayer *p;
    if (!mp_live() || tag < TAG0) {
        MIDI_SEND(mgr, pk, din, usb);
        return;
    }
    q = tag - TAG0;
    p = &S->pl[q / 12];
    port = (q % 12) / 4;
    mode = q % 4;
    if (port == PORT_DIN)
        usb = 0;
    else if (port == PORT_USB)
        din = 0;
    note = pk[1];
    if (msg[0] == 0x51) {
        for (i = 0; i < 16; i++) {
            ch = at(p, i);
            if ((p->busy >> ch & 1) && p->note[ch] == note)
                break;
        }
        if (i < 16) {                                  /* its own channel, oldest of that note first */
            p->busy &= (unsigned short)~(1u << ch);
            touch(p, ch);
            pk[0] = (unsigned char)(0x80 | ch);
        } else if (members(mode, msg[0xC] & 15, C_LAST(p->cfg))) {
            return;                                    /* stolen: its off went out then */
        }
    } else if (msg[0] == 0x50 && mode != MODE_OFF) {
        cfg = p->cfg;
        if (C_MODE(cfg) != mode)                       /* the layer changed since: the note's own mode */
            cfg = (unsigned short)((cfg & ~(3u << 2)) | mode << 2);
        m = members(mode, msg[0xC] & 15, C_LAST(cfg));
        if (!m) {                                      /* Lower / Upper on its master alone */
            MIDI_SEND(mgr, pk, din, usb);
            return;
        }
        if ((p->sent & 15) != (p->gen & 15)) {
            p->sent = (unsigned char)((p->sent & 0xF0) | (p->gen & 15));
            setup(mgr, mode, m, C_BEND(cfg), din, usb);
        }
        for (i = 0; i < 16; i++) {                     /* free longest */
            ch = at(p, i);
            if ((m >> ch & 1) && !(p->busy >> ch & 1))
                break;
        }
        if (i == 16) {                                 /* all busy: the oldest note goes */
            for (i = 0; i < 16; i++) {
                ch = at(p, i);
                if (m >> ch & 1)
                    break;
            }
            send3(mgr, 0x80 | ch, p->note[ch], 100, din, usb);
        }
        p->busy |= (unsigned short)(1u << ch);
        p->note[ch] = (unsigned char)note;
        touch(p, ch);
        pk[0] = (unsigned char)(0x90 | ch);
    }
    if (din || usb)
        MIDI_SEND(mgr, pk, din, usb);
}

/* Pcm_PollMidi's dispatch (0x0804D018, audio task), for every message in the MIDI queue, in order
 * with the notes: msg +0 type, +8 key, +0xC layer (0x4E a pass starts, 0x69 a layer takes over, 0x4F
 * stopped). pcm = Pcm_PollMidi's r7 (pcm + 0xA000): +0x68B DIN, +0x68D USB Dev Out, as for notes. */
void mpe_b2_msg(const unsigned char *msg, const unsigned char *pcm)
{
    int idx;
    unsigned type = msg[0], layer, cfg, ch, mode, port, i;
    int din, usb;
    const unsigned char *pc;
    MpPlayer *p;
    if (!mp_live() || (type != 0x4E && type != 0x4F && type != 0x69) ||
        (idx = key_idx(*(const unsigned *)(msg + 8))) < 0)
        return;
    p = &S->pl[idx];
    if (type == 0x4F) {
        p->sent = (unsigned char)((p->sent & 15) | PC_NONE << 4);
        return;
    }
    layer = msg[0xC];
    if (layer > 3)
        return;
    if (type == 0x4E && B->magic == MP_BKP_MAGIC && B->seen[idx] != B->starts[idx])
        B->seen[idx] = B->starts[idx];                 /* the first pass after a start: always */
    else if ((unsigned)(p->sent >> 4) == layer)
        return;
    p->sent = (unsigned char)((p->sent & 15) | layer << 4);
    pc = p->pc[layer];
    if (!p->outch || !(pc[0] | pc[1] | pc[2]))
        return;
    cfg = p->cfg;
    mode = C_MODE(cfg);
    port = C_PORT(cfg);
    ch = mode == MODE_LOWER ? 0 : mode == MODE_UPPER ? 15 : p->outch - 1u;
    din = pcm[0x68B] && port != PORT_USB;
    usb = pcm[0x68D] && port != PORT_DIN;
    if (!din && !usb)
        return;
    for (i = 0; i < 2; i++)
        if (pc[i])
            send3(MIDI_MGR, 0xB0 | ch, i ? 32 : 0, pc[i] - 1u, din, usb);
    if (pc[2]) {
        unsigned char pk[8] = {(unsigned char)(0xC0 | ch), (unsigned char)(pc[2] - 1u), 0, 0, 2};
        MIDI_SEND(MIDI_MGR, pk, din, usb);
    }
}
