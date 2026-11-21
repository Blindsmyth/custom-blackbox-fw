/* Seq MIDI out (3.1.U): what bank 1 (mpe.c) and bank 2 (mpe_b2.c) share: the state in SRAM4 and
 * backup SRAM, the tag, the ids and the stock functions both call. docs/mpe-out.md. */
#ifndef MPE_H
#define MPE_H

#include <stdint.h>

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

typedef void (*send_fn)(void *, const unsigned char *, int, int);
typedef void (*alloff_fn)(void *, int, void *, int);
typedef void (*liveoff_fn)(void *, void *);
#define MIDI_SEND    ((send_fn)0x08044A0Fu)       /* (mgr, packet, DIN, USB): 3 bytes + length at +4 */
#define ALL_OFF      ((alloff_fn)0x0805D56Du)     /* (player, offset, router, 1): offs for the step's notes */
#define LIVE_OFF     ((liveoff_fn)0x0805E0F9u)    /* (player, router): offs for live notes (+0xC28) */

#define PL_KEY       0x18
#define PL_TAG       0x68

/* Backup SRAM: the "---", "1" .. "128" labels of Bank / Sub / Pgm, built at boot (mpe_register), so
 * the 0.9 KB table is not in bank 1's flash. Pointers, then the strings. */
#define PC_LABELS    129
#define MP_LABELS    0x38800100u   /* const char *[129] */
#define MP_LABEL_TXT 0x38800304u   /* the strings, 408 bytes, to 0x3880049C */
typedef char mp_labels_fit[(MP_LABEL_TXT + 408u <= MP_BKP) ? 1 : -1];

/* RbExt words (rubber.h) holding bank 2's entries: bank 1's stubs call them once bank 2 is paired */
typedef int (*mpe_param_fn)(unsigned char *pl, void *router, const unsigned *msg);
typedef void (*mpe_out_fn)(void *mgr, unsigned char *pk, int din, int usb, const unsigned char *msg);
typedef void (*mpe_msg_fn)(const unsigned char *msg, const unsigned char *pcm);

static inline int mp_live(void) { return S->magic == MP_MAGIC; }

static inline int key_idx(unsigned key)
{
    unsigned r = (key >> 8) & 0xFF, c = key & 0xFF;
    if ((key >> 16) != 1 || r > 3 || c > 3)
        return -1;
    return (int)(r * 4 + c);
}

static inline int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

#endif
