/* Pairing: is bank 2 the payload this image was linked with, all of it, readable? Runs once per
 * boot from state_init (repitch.S), before the audio engine can reach the Rubber voice. On
 * success it boots bank 2 (rubber_boot) and marks the voice READY; otherwise Rubber plays stock
 * grains and its list label reads "Rubber!". */
#include "b2.h"

#define RCC_AHB4ENR (*(volatile uint32_t *)0x580244e0u)
#define PWR_CR1     (*(volatile uint32_t *)0x58024800u)
#define DCCMVAC     (*(volatile uint32_t *)0xe000ef68u)

void rb_bkp_enable(void)
{
    PWR_CR1 |= 1u << 8;          /* DBP: backup domain writable */
    RCC_AHB4ENR |= 1u << 28;     /* BKPRAMEN */
    (void)RCC_AHB4ENR;
    __asm__ volatile("dsb");
    if (*(volatile uint32_t *)RB_BKP != RB_BKP_MAGIC) {
        rb_bkp_put(RB_BKP_MARK, 0);
        rb_bkp_put(RB_BKP_TRIES_ID, 0);
        rb_bkp_put(RB_BKP_TRIES_N, 0);
        rb_bkp_put(RB_BKP_BOOTING, 0);
        rb_bkp_put(RB_BKP, RB_BKP_MAGIC);
    }
}

/* The data cache would hold the write back, and a reset does not write it out. */
void rb_bkp_put(uint32_t at, uint32_t value)
{
    *(volatile uint32_t *)at = value;
    DCCMVAC = at;
    __asm__ volatile("dsb\n isb");
}

uint32_t rb_bkp_get(uint32_t at)
{
    return *(volatile uint32_t *)at;
}

int rb_b2_intact(void)
{
    const volatile RbHeader *h = (const volatile RbHeader *)RB_B2_BASE;
    const volatile uint32_t *p;
    uint32_t len, sum = 0, i;
    if (h->magic != RB_MAGIC || h->id != rb_expect_id)
        return 0;
    len = h->length;
    if (len < 64u || len > RB_B2_LIMIT || (len & 31u) || h->sum != rb_expect_sum)
        return 0;
    FLASH_CCR2 = FLASH_ECC;
    p = (const volatile uint32_t *)(RB_B2_BASE + 16u);
    for (i = 16u; i < len; i += 4u)
        sum += *p++;
    return sum == rb_expect_sum && !(FLASH_SR2 & FLASH_ECC);
}

static void label(RbVoice *v, const char *s)
{
    int i = 0;
    for (; s[i] && i < 15; i++)
        v->label[i] = s[i];
    v->label[i] = 0;
}

/* Called by state_init every boot. SRAM4 survives a soft reset, so the voice is reset here. */
void rb_pair(void)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    int ok = 0;
    v->magic = RB_VOICE_MAGIC;
    v->owner = 0;
    v->state = RB_ST_UNKNOWN;
    v->reset_req = 0;
    v->block_done = 0;
    v->prev_active = 0;
    v->avg_cyc = v->peak_cyc = 0;
    label(v, "Rubber!");
    rb_bkp_enable();
    if (rb_bkp_get(RB_BKP_MARK) != RB_MARK_SCAN) {
        /* A read of a bad flash word faults; the mark tells the next boot to keep off. */
        rb_bkp_put(RB_BKP_MARK, RB_MARK_SCAN);
        ok = rb_b2_intact();
        rb_bkp_put(RB_BKP_MARK, 0);
    }
    if (ok && rb_bkp_get(RB_BKP_BOOTING) == RB_MARK_BOOT) {
        /* The last boot never came back from rubber_boot: keep Rubber off rather than loop.
         * A new payload (the loader clears the mark after writing one) gets a fresh try. */
        label(v, "Rubber!!");
        ok = 0;
    } else if (ok) {
        const volatile RbHeader *h = (const volatile RbHeader *)RB_B2_BASE;
        rb_bkp_put(RB_BKP_BOOTING, RB_MARK_BOOT);
        ((rb_boot_fn)h->boot)();
        rb_bkp_put(RB_BKP_BOOTING, 0);
    }
    v->state = ok ? RB_ST_READY : RB_ST_ABSENT;
}

/* repitch.S hook_lane: may the Rubber voice render? */
int rb_ready(void)
{
    const RbVoice *v = (const RbVoice *)RB_VOICE;
    return v->magic == RB_VOICE_MAGIC && (v->state & 0xffu) == RB_ST_READY;   /* b2load.c sets 0x100 */
}

void rb_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    const volatile RbHeader *h = (const volatile RbHeader *)RB_B2_BASE;
    if (rb_ready())
        ((rb_mix_fn)h->mix)(clip, lane, mix, ctx);
}

/* The expectations the build fills in (b2_finish in build_patch.py). In .rodata, so in the cave. */
__attribute__((used)) const volatile uint32_t rb_expect_id = 0xffffffffu;
__attribute__((used)) const volatile uint32_t rb_expect_sum = 0xffffffffu;
