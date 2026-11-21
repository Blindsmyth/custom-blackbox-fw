/* Master chain on the Blackbox: what bank 1 (song-dj/mixer.c) and bank 2 (mc_b2.c) share.
 * docs/master-chain.md.
 *
 * Bank 1 keeps the parameters as the DSP wants them (McParams, floats) in SRAM4, refreshed from
 * the global bag on the audio task, and calls bank 2 through RbExt.master (tools/stretch/rubber/
 * rubber.h) for each stage. Bank 2 keeps its own state (McChain) in B2RAM and writes the meters
 * back here for the UI. */
#ifndef MC_FW_H
#define MC_FW_H

#include "mc_dsp.h"

#define MC_SHARED_ADDR  0x38007CA0u   /* SRAM4, after MxState (mixer.c checks both ends) */
#define MC_SHARED_END   0x38008000u   /* Bungee's grain input */
#define MC_SHARED_MAGIC 0x3143534Du   /* "MSC1" */

/* RbExt.master(op, l, r, n): one stage on bus 0, in place */
enum {
    MC_OP_EQ = 1,       /* 5-band dynamic EQ (in the master EQ's slot) */
    MC_OP_PRE = 2,      /* CLR, then CMP (Squish or the stock compressor model) */
    MC_OP_MAX = 3,      /* the maximizer */
    MC_OP_RESET = 4,    /* forget all state (chain switched on, preset loaded) */
    MC_OP_LIMIT = 5     /* the chain off: the stock limiter model only, as a safety */
};

/* RbExt.ui(op, a, b, c): UI-task drawing in bank 2, called directly (not on the audio's stack) */
enum {
    MC_UI_METER = 1,    /* (widget, ctx, kind): a GR bar with its max-hold marker in the widget */
    MC_UI_TEXT = 2      /* (char *out[32], kind): "GR -3.2  MAX -6.0" */
};
enum { MC_KIND_CMP = 1, MC_KIND_MAX = 2 };
#define MC_MIRROR_N 20      /* >= mixer.c MC_NSTORED */

typedef struct {
    unsigned magic;
    unsigned seq;        /* bumped by bank 1 whenever p changes */
    float bpm;           /* engine tempo, 0 = unknown */
    McParams p;          /* written by bank 1 (audio task) */
    McMeters m;          /* written by bank 2 (audio task), read by the UI */
    unsigned tick;       /* bank 1: audio blocks seen (the bag is read every 8th) */
    unsigned char was_on;/* bank 1: the chain ran last block (off -> on resets bank 2's state) */
    unsigned char hold_req;   /* UI: bumped by BACK on CMP / MAX: clear the holds */
    unsigned char hold_seen;  /* bank 2: the last hold_req it cleared for */
    unsigned char values_known, values_last;  /* UI: Values (Global / Preset) as last seen */
    float cmp_hold, max_hold; /* bank 2: deepest gain reduction since BACK (dB, <= 0) */
    float eq_hold[MC_BANDS];  /* ... per EQ band (signed: the largest dynamic gain change) */
    unsigned char mirror_ok;  /* UI: mirror[] holds the live values the preset's session last got */
    int mirror[MC_MIRROR_N];  /* UI (mixer.c): only a change from these (a knob) goes to the session */
} McShared;

#define MC_SHARED ((McShared *)MC_SHARED_ADDR)

typedef char mc_shared_fits[(MC_SHARED_ADDR + sizeof(McShared) <= MC_SHARED_END) ? 1 : -1];

#endif
