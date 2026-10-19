/* Warp "Rubber": what bank 1 (repitch.S, b2pair.c) and bank 2 (rubber.cpp) share.
 *
 * Bank 2 starts at 0x08100000 with RbHeader; bank 1 calls bank 2 only through its table, and
 * only after b2pair.c found the payload intact (magic, id, length, sum, ECC scan).
 * The voice header in SRAM4 is written by the lane hook (owner, block) and the grain hook
 * (reset request); the offsets match Capi's so the asm is the same. */
#ifndef RUBBER_H
#define RUBBER_H

#include <stdint.h>

#define RB_B2_BASE     0x08100000u
#define RB_B2_LIMIT    0x000C0000u    /* sectors 0-5: payload; the loader erases nothing else */
#define RB_B2_CLAIM_AT 0x000C0000u    /* sector 6, first flash word: "bank 2 is ours". Never erased */
#define RB_B2_END      0x000E0000u    /* end of sector 6. Sector 7 (0x081E0000) is factory calibration */
#define RB_B2_SECTOR   0x00020000u
#define RB_MAGIC       0x32424252u    /* "RBB2" */
#define RB_CLAIM       0x43424252u    /* "RBBC" */

typedef int (*rb_boot_fn)(void);   /* 1 = started, 0 = not (out of memory) */
typedef void (*rb_mix_fn)(unsigned clip, unsigned lane, unsigned mix, unsigned ctx);

/* The first 32 bytes of bank 2 (one flash word). The loader writes it last. */
typedef struct RbHeader {
    uint32_t magic;
    uint32_t id;          /* hash of the bank 2 sources, set by the build */
    uint32_t length;      /* bytes of payload from RB_B2_BASE, a multiple of 32 */
    uint32_t sum;         /* sum of the 32-bit words from offset 16 to length */
    uint32_t boot;        /* rb_boot_fn | 1 */
    uint32_t mix;         /* rb_mix_fn | 1 */
    uint32_t reserved[2];
} RbHeader;

/* Voice header, SRAM4 (free on main; Capi used the same slot on warp-capi). */
#define RB_VOICE       0x38007000u
#define RB_VOICE_MAGIC 0x52425631u    /* "1VBR" */
typedef struct RbVoice {
    uint32_t magic;       /* 0 */
    uint32_t owner;       /* 4  clip pointer that may render, 0 = free (repitch.S SM_OWNER) */
    uint32_t state;       /* 8  RB_ST_* */
    uint32_t avg_cyc;     /* 12 rubber_mix cycles per block, last second */
    uint32_t peak_cyc;    /* 16 */
    int32_t reset_req;    /* 20 a new grain on the owner (repitch.S SM_RESET) */
    int32_t block_done;   /* 24 one render per audio block (repitch.S SM_BLOCK) */
    int32_t prev_active;  /* 28 */
    char label[16];       /* 32 the Warp list label: "Rubber", "Rubber!", "Rubber 54%" */
    uint32_t cyc_avg;     /* 48 rb_mix's cycles per block, smoothed over ~16 blocks (b2pair.c) */
    uint32_t over_n;      /* 52 blocks in a row with cyc_avg over RB_GUARD_CYC or the audio task over RB_GUARD_LOAD */
    uint32_t cyc_peak;    /* 56 worst rb_mix block since the Tools bar last showed it (diag.c) */
} RbVoice;

#define RB_ST_UNKNOWN  0u
#define RB_ST_READY    1u             /* paired and booted: hook_lane may render */
#define RB_ST_ABSENT   2u             /* no valid payload in bank 2: Rubber plays stock grains */
#define RB_ST_PAIRED   3u             /* payload intact, not started yet (b2pair.c rb_start) */
#define RB_ST_BROKEN   4u             /* payload intact, but the last start never came back: off */
#define RB_ST_SLOW     5u             /* switched itself off: too slow for the audio block */

/* The CPU guard (b2pair.c rb_mix): a 256-frame block is 2,560,000 cycles at 480 MHz. A render
 * that keeps the audio task busy past its block starves every other task until the watchdog
 * resets the unit, so the leaf turns itself off after RB_GUARD_N blocks in a row with its smoothed
 * cost over this, or with the whole audio task over RB_GUARD_LOAD (b2pair.c rb_mix). */
#define RB_BLOCK_CYC   2560000u
#define RB_GUARD_CYC   (RB_BLOCK_CYC / 4u * 3u)
#define RB_GUARD_N     32u
#define RB_GUARD_LOAD  950u           /* per mille: the whole audio task (diag.c diag_load) */

/* Rubber Band runs on its own stack at the top of SRAM1-3 (rb_call_on_stack, rubber_cave.S),
 * not on the audio or disk task's: its render needs ~5 KB. rubber.ld keeps .data/.bss and the
 * heap (rt.c) below. */
#define RB_STACK_TOP   0x30048000u
#define RB_STACK_SIZE  0x00003000u    /* 12 KB: the render needs 5.2 KB (bench) */

#endif
