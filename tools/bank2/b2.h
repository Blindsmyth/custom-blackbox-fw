/* Flash bank 2, bank 1 side: pairing (b2pair.c) and the card loader (b2load.c).
 *
 * Our own implementation of the mechanism blackbox-mod proved on hardware (reference/blackbox-mod
 * src/b2load.c, b2head.c; see docs/re-notes.md "Findings from blackbox-mod"). Rules kept:
 *   - only bank 2's own flash registers (KEYR2/CR2/SR2/CCR2), never bank 1's;
 *   - payload in sectors 0-5, a claim word at the start of sector 6, never sector 7 (factory
 *     calibration at 0x081E0000);
 *   - the header flash word is written last, so a cut-off write reads as "no payload";
 *   - nothing reads bank 2 while it is written: the loader only runs when pairing said "absent";
 *   - a scan that faults leaves a mark in backup SRAM, so the next boot keeps off bank 2;
 *   - at most RB_MAX_TRIES programming attempts per payload, counted in backup SRAM.
 * Cave code has no .data/.bss: state lives in SRAM4 (RbVoice) and backup SRAM. */
#ifndef B2_H
#define B2_H

#include <stdint.h>

#include "rubber.h"

/* Filled in by the build after the link (placeholder 0xFFFFFFFF): what this image expects. */
extern const volatile uint32_t rb_expect_id;
extern const volatile uint32_t rb_expect_sum;

/* Backup SRAM (survives reset). Stock never enables it; nothing else of ours uses it. */
/* RbVoice.state bit: the loader has had its one go this boot (b2load.c; b2msg.c reads it). */
#define RB_ST_LOOKED    0x100u

#define RB_BKP          0x38800000u
#define RB_BKP_MAGIC    0x4b424252u   /* "RBBK" */
#define RB_BKP_MARK     (RB_BKP + 4u) /* RB_MARK_SCAN while a read of bank 2 is in flight */
#define RB_BKP_TRIES_ID (RB_BKP + 8u)
#define RB_BKP_TRIES_N  (RB_BKP + 12u)
#define RB_MARK_SCAN    0x4e435352u   /* "RSCN" */
#define RB_BKP_BOOTING  (RB_BKP + 16u) /* RB_MARK_BOOT while rubber_boot runs */
#define RB_MARK_BOOT    0x544f4252u   /* "RBOT": a boot that died in bank 2 leaves this */
#define RB_MAX_TRIES    2u

/* Where the last boot got to (rb_stage). A boot that dies leaves its stage here; the next boot
 * moves it to RB_BKP_LAST and shows it after the Rubber label ("Rubber!! 11"). 0 = nothing in
 * flight. */
#define RB_BKP_STAGE    (RB_BKP + 20u)
#define RB_BKP_LAST     (RB_BKP + 24u)
#define RB_SG_SCAN      0x10u   /* b2pair: reading bank 2 for the pairing check */
#define RB_SG_BOOT      0x11u   /* b2pair: rubber_boot (bank 2) */
#define RB_SG_FILE      0x20u   /* b2load: reading BBRUBBER.BIN */
#define RB_SG_CLAIM     0x21u   /* b2load: blank check / claim word */
#define RB_SG_ERASE     0x30u   /* + sector */
#define RB_SG_WRITE     0x38u   /* b2load: programming */
#define RB_SG_VERIFY    0x3au   /* b2load: rescan after programming */

/* Crash records (tools/diag/diag.c), after ours and below 0x38800100. Each has a live copy the
 * handlers write and a saved copy that rb_pair latches once per boot, so the boot after a crash
 * can show it (Tools bar) while the running unit never overwrites it. */
#define RB_BKP_FAULT    (RB_BKP + 0x40u)   /* 12 words: magic, PC, LR, xPSR, CFSR, HFSR, MMFAR, BFAR, EXC_RETURN, SP, frame ok, stage */
#define RB_BKP_FAULT_SV (RB_BKP + 0x70u)   /* 12 words */
#define RB_BKP_WDG      (RB_BKP + 0xa0u)   /* 8 words: magic, PC, LR, xPSR, stamp A age, stamp B age, counter, stage */
#define RB_BKP_WDG_SV   (RB_BKP + 0xc0u)   /* 8 words, ends at RB_BKP + 0xe0 */
#define RB_FAULT_MAGIC  0x31544c46u        /* "FLT1" */
#define RB_WDG_MAGIC    0x31474457u        /* "WDG1" */

#define FLASH_KEYR2 (*(volatile uint32_t *)0x52002104u)
#define FLASH_CR2   (*(volatile uint32_t *)0x5200210cu)
#define FLASH_SR2   (*(volatile uint32_t *)0x52002110u)
#define FLASH_CCR2  (*(volatile uint32_t *)0x52002114u)
#define FLASH_ECC   ((1u << 25) | (1u << 26))  /* SNECCERR2 | DBECCERR2 */

void rb_bkp_enable(void);
void rb_bkp_put(uint32_t at, uint32_t value);
uint32_t rb_bkp_get(uint32_t at);

/* Read the header and every word of bank 2 under the ECC flags. 1 = intact and ours. */
int rb_b2_intact(void);

/* Note the stage in backup SRAM (written through to the SRAM, so a crash keeps it). */
void rb_stage(uint32_t stage);
/* Clean every cache line of backup SRAM out to the SRAM (before a reset). */
void rb_bkp_flush(void);
/* rubber_cave.S: fn(a, b, c, d) on Rubber's own stack (RB_STACK_TOP). */
uint32_t rb_call_on_stack(uint32_t fn, unsigned a, unsigned b, unsigned c, unsigned d);   /* fn's r0 */
void rb_call_at(uint32_t fn, uint32_t stack_top);   /* fn() on the stack ending at stack_top (cave asm) */
void rb_work(void);                                 /* b2pair.c: one wake of the leaf's task */
int rb_ready(void);                                 /* b2pair.c: may the leaf render */
int rb_lanes(unsigned clip, unsigned lane, unsigned mix, unsigned ctx);   /* b2pair.c: Fill / Slice / Grain */
/* b2pair.c, from the card loader's poll (pcmStreamer): start bank 2 once pairing found it. */
void rb_start(void);
/* b2pair.c: append the loader's one-character verdict to the Rubber label. */
void rb_note(char c);
/* diag.c, once per boot from rb_pair: latch the crash records of the boot before. */
void diag_boot(void);
uint32_t diag_load(void);       /* diag.c: whole audio-task load, per mille, smoothed; 0 = not known yet */

#endif
