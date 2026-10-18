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
#define RB_BKP          0x38800000u
#define RB_BKP_MAGIC    0x4b424252u   /* "RBBK" */
#define RB_BKP_MARK     (RB_BKP + 4u) /* RB_MARK_SCAN while a read of bank 2 is in flight */
#define RB_BKP_TRIES_ID (RB_BKP + 8u)
#define RB_BKP_TRIES_N  (RB_BKP + 12u)
#define RB_MARK_SCAN    0x4e435352u   /* "RSCN" */
#define RB_BKP_BOOTING  (RB_BKP + 16u) /* RB_MARK_BOOT while rubber_boot runs */
#define RB_MARK_BOOT    0x544f4252u   /* "RBOT": a boot that died in bank 2 leaves this */
#define RB_MAX_TRIES    2u

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

#endif
