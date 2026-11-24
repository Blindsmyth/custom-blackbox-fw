/* Seq MIDI out: MPE channel spread and output port per sequence (3.1.U). Design and the stock code it
 * uses: docs/mpe-out.md.
 *
 * Every note a sequence sends out goes through one engine message (0x50 on, 0x51 off) whose byte +8
 * is the player's +0x68 (stock's dormant `midiport`) and +0xC its +0x69 (the MIDI Out channel).
 * Stock posts them from six places (the step's notes, their offs, all-off, the layer switch, live
 * notes and their all-off) and sends them all from one call in pcmStreamer, `bl FUN_08044A0E` at
 * 0x0804D004. So:
 *   - the audio task keeps player+0x68 = a tag: which of the 16 sequence players it is, and the
 *     sequence's Port and MPE mode (mpe_param, from the player's own param switch);
 *   - pcmStreamer, at the send, reads the tag back from the message and picks the channel
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
 * pcmStreamer sees the player's own pass (0x4E, every pass) and layer (0x69) messages in the same
 * queue as its notes (mpe_msg), and sends a layer's set on the first pass after each Player_Start
 * (counted by mpe_player_start in backup SRAM, as there are stop paths that post no 0x4F) and when
 * another layer takes over. Channel: MIDI Out, or the MPE master (Lower 1, Upper 16).
 *   0x1EF "Loop:"    Off / On (default On), per sequence: Off plays one pass. mpe_pass sets the
 *                    player's stop request (+0x32) as the pass starts, so stock's own stop at the
 *                    loop end (0x4F, all-off) ends it there, as a quantised stop does.
 * Ids 0x1E5 (stock App_SetParam's own) and the Song DJ row's widgets (to 0x1E4) are taken.
 */

#define MP_BASE      0x38007148u   /* SRAM4: after RbVoice + diag (0x38007000-0x38007148) */
#define MP_END       0x38007400u   /* Song DJ row's DjState */
#define MP_MAGIC     0x3145504Du   /* 'MPE1' */
#define N_PL         16            /* sequence players: key 0x1RRCC, rows and columns 0..3 */

#define ID_PORT      0x74
#define ID_MODE      0x1F0
#define ID_LAST      0x1F1
#define ID_OUTCH     0x6B          /* MIDI Out: the first note channel */
#define ID_BEND      0x1F2
#define ID_LOOP      0x1EF         /* per sequence, with Port .. PB Rng (0x1EF-0x1F2 contiguous) */
#define ID_BANK      0x1EC         /* Bank, Sub, Pgm: per layer */
#define MP_BKP       0x38800600u   /* backup SRAM: start counters (below usbdev's 0x38800650) */
#define MP_BKP_MAGIC 0x3150504Du   /* 'MPP1' */
#define PL_STOP      0x32          /* player: stop at the next pass start (stock's quantised stop) */
#define ID_PGM       0x1EE
#define PC_NONE      7             /* sent: no layer's program change out since a stop */
#define MIDI_MGR     ((void *)0x24002E88u)   /* Pcm_PollMidi's FUN_08044A0E object (0x0804D174) */

enum { PORT_BOTH, PORT_DIN, PORT_USB, PORTS };
enum { MODE_OFF, MODE_LOWER, MODE_UPPER, MODE_MULTI, MODES };
#define LAST_DEF     16
#define BEND_DEF     48
#define BEND_MAX     96

/* cfg: port | mode << 2 | (last - 1) << 4 | bend << 8 | loop off << 15 */
#define C_PORT(c)    ((c) & 3)
#define C_MODE(c)    (((c) >> 2) & 3)
#define C_LAST(c)    (((c) >> 4) & 15)    /* 0-based */
#define C_BEND(c)    (((c) >> 8) & 127)
#define C_ONCE(c)    (((c) >> 15) & 1)
#define CFG(p, m, n, b) ((unsigned short)((p) | (m) << 2 | (n) << 4 | (b) << 8))

/* player+0x68 tag: 64 + idx * 12 + port * 4 + mode. Stock's own values there are 0 (nothing in
 * stock sets it) and the key-0x400 node's +0x294 port (1..4), both below 64. */
#define TAG0         64
#define TAG(i, p, m) (TAG0 + (i) * 12 + (p) * 4 + (m))

/* Packed to fit between diag and DjState: the halfwords of odd players are unaligned, which plain
 * LDRH / STRH take on the M7 (never LDM / LDRD here). */
typedef struct __attribute__((packed)) {
    unsigned char note[16];        /* the note held on each channel, where `busy` has its bit */
    unsigned char order[8];        /* the 16 channels as nibbles, least recently used first */
    unsigned short busy;           /* channels with a note on */
    unsigned short cfg;            /* port | mode | last | bend (audio task) */
    unsigned char pc[4][3];        /* per layer: bank, sub, program; 0 = ---, else 1..128 (audio task) */
    unsigned char gen;             /* bumped when the settings are posted (audio task) */
    unsigned char outch;           /* MIDI Out (0x6B): 0 off, else channel 1..16 (audio task) */
    unsigned char sent;            /* gen & 15 whose MPE setup went out | the layer whose program
                                      change went out << 4, PC_NONE after a stop (pcmStreamer) */
} MpPlayer;

typedef struct {
    unsigned magic;
    MpPlayer pl[N_PL];
} MpState;

typedef char mp_player_size[(sizeof(MpPlayer) == 43) ? 1 : -1];
typedef char mp_fits[(MP_BASE + sizeof(MpState) <= MP_END) ? 1 : -1];
typedef char mp_after_diag[(MP_BASE >= 0x38007148u) ? 1 : -1];

#define S ((MpState *)MP_BASE)

typedef struct {
    unsigned magic;
    unsigned char starts[N_PL];    /* Player_Start calls (audio task) */
    unsigned char seen[N_PL];      /* starts whose program change went out (pcmStreamer) */
} MpBkp;
#define B ((volatile MpBkp *)MP_BKP)
void rb_bkp_enable(void);          /* tools/bank2/b2pair.c: backup SRAM clock and writes */

typedef void (*reg_enum_fn)(void *, unsigned, const char *, const char *const *, int, const char *);
typedef void (*reg_num_fn)(void *, unsigned, int, const char *, int, int, const char *);
typedef void (*param_add_fn)(void *, unsigned, int);
typedef void (*send_fn)(void *, const unsigned char *, int, int);
typedef void (*alloff_fn)(void *, int, void *, int);
typedef void (*liveoff_fn)(void *, void *);
typedef void (*start_fn)(void *, int, int);
#define PARAM_ENUM   ((reg_enum_fn)0x0808C0D9u)   /* reg, id, title, labels, count, xml */
#define PARAM_NUMBER ((reg_num_fn)0x0808C12Du)    /* reg, id, type, title, min, max, xml */
#define PARAM_ADD    ((param_add_fn)0x08093EF7u)  /* bag, id, default */
#define MIDI_SEND    ((send_fn)0x08044A0Fu)       /* (mgr, packet, DIN, USB): 3 bytes + length at +4 */
#define ALL_OFF      ((alloff_fn)0x0805D56Du)     /* (player, offset, router, 1): offs for the step's notes */
#define LIVE_OFF     ((liveoff_fn)0x0805E0F9u)    /* (player, router): offs for live notes (+0xC28) */
#define PLAYER_START ((start_fn)0x0805CB65u)      /* Player_Start(player, transport, router) */

#define PL_KEY       0x18
#define PL_TAG       0x68

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
/* "---", "1" .. "128" */
#define PC_LABELS    129
static const char pc_s[] =   /* \000 so a digit after it is not part of the escape */
    "---\0001\0002\0003\0004\0005\0006\0007\0008\0009\00010\00011\00012\00013\00014\00015\000"
    "16\00017\00018\00019\00020\00021\00022\00023\00024\00025\00026\00027\00028\00029\00030\00031\000"
    "32\00033\00034\00035\00036\00037\00038\00039\00040\00041\00042\00043\00044\00045\00046\00047\000"
    "48\00049\00050\00051\00052\00053\00054\00055\00056\00057\00058\00059\00060\00061\00062\00063\000"
    "64\00065\00066\00067\00068\00069\00070\00071\00072\00073\00074\00075\00076\00077\00078\00079\000"
    "80\00081\00082\00083\00084\00085\00086\00087\00088\00089\00090\00091\00092\00093\00094\00095\000"
    "96\00097\00098\00099\000100\000101\000102\000103\000104\000105\000106\000107\000108\000109\000110\000111\000"
    "112\000113\000114\000115\000116\000117\000118\000119\000120\000121\000122\000123\000124\000125\000126\000127\000"
    "128\000";
static const char *const pc_names[PC_LABELS] = {
    pc_s + 0, pc_s + 4, pc_s + 6, pc_s + 8, pc_s + 10, pc_s + 12, pc_s + 14, pc_s + 16,
    pc_s + 18, pc_s + 20, pc_s + 22, pc_s + 25, pc_s + 28, pc_s + 31, pc_s + 34, pc_s + 37,
    pc_s + 40, pc_s + 43, pc_s + 46, pc_s + 49, pc_s + 52, pc_s + 55, pc_s + 58, pc_s + 61,
    pc_s + 64, pc_s + 67, pc_s + 70, pc_s + 73, pc_s + 76, pc_s + 79, pc_s + 82, pc_s + 85,
    pc_s + 88, pc_s + 91, pc_s + 94, pc_s + 97, pc_s + 100, pc_s + 103, pc_s + 106, pc_s + 109,
    pc_s + 112, pc_s + 115, pc_s + 118, pc_s + 121, pc_s + 124, pc_s + 127, pc_s + 130, pc_s + 133,
    pc_s + 136, pc_s + 139, pc_s + 142, pc_s + 145, pc_s + 148, pc_s + 151, pc_s + 154, pc_s + 157,
    pc_s + 160, pc_s + 163, pc_s + 166, pc_s + 169, pc_s + 172, pc_s + 175, pc_s + 178, pc_s + 181,
    pc_s + 184, pc_s + 187, pc_s + 190, pc_s + 193, pc_s + 196, pc_s + 199, pc_s + 202, pc_s + 205,
    pc_s + 208, pc_s + 211, pc_s + 214, pc_s + 217, pc_s + 220, pc_s + 223, pc_s + 226, pc_s + 229,
    pc_s + 232, pc_s + 235, pc_s + 238, pc_s + 241, pc_s + 244, pc_s + 247, pc_s + 250, pc_s + 253,
    pc_s + 256, pc_s + 259, pc_s + 262, pc_s + 265, pc_s + 268, pc_s + 271, pc_s + 274, pc_s + 277,
    pc_s + 280, pc_s + 283, pc_s + 286, pc_s + 289, pc_s + 292, pc_s + 296, pc_s + 300, pc_s + 304,
    pc_s + 308, pc_s + 312, pc_s + 316, pc_s + 320, pc_s + 324, pc_s + 328, pc_s + 332, pc_s + 336,
    pc_s + 340, pc_s + 344, pc_s + 348, pc_s + 352, pc_s + 356, pc_s + 360, pc_s + 364, pc_s + 368,
    pc_s + 372, pc_s + 376, pc_s + 380, pc_s + 384, pc_s + 388, pc_s + 392, pc_s + 396, pc_s + 400,
    pc_s + 404,
};

static int live(void) { return S->magic == MP_MAGIC; }

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
        PARAM_ENUM(reg, ID_BANK + i, t_pc[i], pc_names, PC_LABELS, x_pc[i]);
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

static int key_idx(unsigned key)
{
    unsigned r = (key >> 8) & 0xFF, c = key & 0xFF;
    if ((key >> 16) != 1 || r > 3 || c > 3)
        return -1;
    return (int)(r * 4 + c);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The player's param switch (0x0805EBCE, audio task), for every 0x39 message to this player:
 * msg +0xC id, +0x10 value, +0x14 layer. Returns 1 when stock's switch must not run (Port: stock's
 * case would store the raw value and compare it with the tag). Ours are layer 0's: a layer push
 * also posts them for layers 1-3 (its id list is the sequence template), and those are dropped.
 * Any post of them (an edit, a preset load) or of MIDI Out has the MPE setup sent again. */
int mpe_param(unsigned char *pl, void *router, const unsigned *msg)
{
    int idx, handled = 1;
    unsigned id, old, now, tag, was;
    int value;
    MpPlayer *p;
    if (!live() || (idx = key_idx(*(unsigned *)(pl + PL_KEY))) < 0)
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

/* ---- pcmStreamer: the send at 0x0804D004 */

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

/* `bl FUN_08044A0E` at 0x0804D004 (pcmStreamer), for engine messages 0x50-0x53: (mgr, packet, DIN,
 * USB) as stock, plus the message (+0 type, +8 tag, +0xC MIDI Out channel, +0x10 note). */
void mpe_out(void *mgr, unsigned char *pk, int din, int usb, const unsigned char *msg)
{
    unsigned tag = msg[8], q, port, mode, cfg, m, i, ch, note;
    MpPlayer *p;
    if (!live() || tag < TAG0) {
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

/* Pcm_PollMidi's dispatch (0x0804D018, pcmStreamer), for every message in the MIDI queue, in order
 * with the notes: msg +0 type, +8 key, +0xC layer (0x4E a pass starts, 0x69 a layer takes over, 0x4F
 * stopped). pcm = Pcm_PollMidi's r7 (pcm + 0xA000): +0x68B DIN, +0x68D USB Dev Out, as for notes. */
void mpe_msg(const unsigned char *msg, const unsigned char *pcm)
{
    int idx;
    unsigned type = msg[0], layer, cfg, ch, mode, port, i;
    int din, usb;
    const unsigned char *pc;
    MpPlayer *p;
    if (!live() || (type != 0x4E && type != 0x4F && type != 0x69) ||
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

/* In place of `bl Player_Start` in the player's event loop (0x0805E9D2, 0x0805EA1A, 0x0805ED04, 0x0805EEDC,
 * audio task): a launch, quantised or not, counts for the program change. */
void mpe_player_start(unsigned char *pl, int transport, int router)
{
    int idx;
    PLAYER_START(pl, transport, router);
    if (live() && B->magic == MP_BKP_MAGIC && (idx = key_idx(*(unsigned *)(pl + PL_KEY))) >= 0)
        B->starts[idx]++;
}

/* Player_Tick, right after a pass starts (its 0x4E post, mpe_pass_thunk; audio task). Loop Off: the
 * stop request, so the pass ends the sequence. */
void mpe_pass(unsigned char *pl)
{
    int idx;
    if (live() && (idx = key_idx(*(unsigned *)(pl + PL_KEY))) >= 0 && C_ONCE(S->pl[idx].cfg))
        pl[PL_STOP] = 1;
}
