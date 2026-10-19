/* Pairing: is bank 2 the payload this image was linked with, all of it, readable? Runs once per
 * boot from state_init (repitch.S), before the audio engine can reach the Rubber voice. On
 * success the voice is PAIRED, and rb_start (from the disk task) boots bank 2 and marks it
 * READY; otherwise Rubber plays stock grains and its list label reads "Rubber!". */
#include "b2.h"

#ifndef RB_LABEL
#define RB_LABEL "Rubber"   /* the Warp list name; the Bungee build: "Bungee" */
#endif

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

void rb_stage(uint32_t stage)
{
    rb_bkp_put(RB_BKP_STAGE, stage);
}

void rb_bkp_flush(void)
{
    uint32_t a;
    for (a = RB_BKP; a < RB_BKP + 0x100u; a += 32u)
        DCCMVAC = a;
    __asm__ volatile("dsb\n isb");
}

static const char hex_digit[] = "0123456789abcdef";

/* "Rubber!" or "Rubber!" plus the stage the boot before died in: "Rubber! 30" = while erasing. */
static void label(RbVoice *v, const char *s)
{
    uint32_t last = rb_bkp_get(RB_BKP_LAST);
    int i = 0;
    for (; s[i] && i < 11; i++)
        v->label[i] = s[i];
    if (last && last < 0x100u) {
        v->label[i++] = ' ';
        v->label[i++] = hex_digit[(last >> 4) & 15u];
        v->label[i++] = hex_digit[last & 15u];
    }
    v->label[i] = 0;
}

/* The card loader's verdict after the label: "Rubber! -". Replaces an earlier one. */
void rb_note(char c)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    int i = 0;
    while (i < 13 && v->label[i])
        i++;
    if (i >= 2 && v->label[i - 2] == ' ' && v->label[i - 1] == '.')
        i -= 2;                   /* the loader's "looked" placeholder */
    v->label[i++] = ' ';
    v->label[i++] = c;
    v->label[i] = 0;
}

/* Called by state_init every boot, while the app is being built (screen task). SRAM4 survives a
 * soft reset, so the voice is reset here. This only checks bank 2; rb_start runs it later, from
 * the disk task, once the unit is up. */
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
    v->cyc_avg = v->over_n = v->cyc_peak = 0;
    rb_bkp_enable();
    diag_boot();                  /* the boot before: crash records, and where it got to */
    rb_bkp_put(RB_BKP_LAST, rb_bkp_get(RB_BKP_STAGE));
    rb_stage(0);
    label(v, RB_LABEL "!");
    if (rb_bkp_get(RB_BKP_MARK) != RB_MARK_SCAN) {
        /* A read of a bad flash word faults; the mark tells the next boot to keep off. */
        rb_bkp_put(RB_BKP_MARK, RB_MARK_SCAN);
        rb_stage(RB_SG_SCAN);
        ok = rb_b2_intact();
        rb_bkp_put(RB_BKP_MARK, 0);
        rb_stage(0);
    }
    if (ok && rb_bkp_get(RB_BKP_BOOTING) == RB_MARK_BOOT) {
        /* The last start never came back from rubber_boot: keep Rubber off rather than loop.
         * The payload is intact, so the loader leaves it alone; a different BBRUBBER.BIN
         * (another build) is what clears this. */
        label(v, RB_LABEL "!!");
        v->state = RB_ST_BROKEN;
    } else {
        if (ok)
            label(v, RB_LABEL "..");   /* intact, starting once the unit is up */
        v->state = ok ? RB_ST_PAIRED : RB_ST_ABSENT;
    }
}

void rb_start(void)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    const volatile RbHeader *h = (const volatile RbHeader *)RB_B2_BASE;
    if (v->magic != RB_VOICE_MAGIC || (v->state & 0xffu) != RB_ST_PAIRED)
        return;
    uint32_t ok;
    rb_bkp_put(RB_BKP_BOOTING, RB_MARK_BOOT);
    rb_stage(RB_SG_BOOT);
    ok = rb_call_on_stack(h->boot, 0, 0, 0, 0);
    rb_bkp_put(RB_BKP_BOOTING, 0);
    rb_stage(0);
    __asm__ volatile("dsb");
    /* Only now may the audio task render. A start that ran out of memory left "Rubber! mem". */
    v->state = (v->state & ~0xffu) | (ok == 1u ? RB_ST_READY : RB_ST_ABSENT);
}

/* repitch.S hook_lane: may the Rubber voice render? */
int rb_ready(void)
{
    const RbVoice *v = (const RbVoice *)RB_VOICE;
    return v->magic == RB_VOICE_MAGIC && (v->state & 0xffu) == RB_ST_READY;   /* b2load.c sets 0x100 */
}

#define DWT_CYCCNT (*(volatile uint32_t *)0xe0001004u)

void rb_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    const volatile RbHeader *h = (const volatile RbHeader *)RB_B2_BASE;
    uint32_t t0, c;
    if (!rb_ready())
        return;
    t0 = DWT_CYCCNT;
    rb_call_on_stack(h->mix, clip, lane, mix, ctx);
    c = DWT_CYCCNT - t0;
    v->cyc_avg = v->cyc_avg - (v->cyc_avg >> 4) + (c >> 4);
    if (c > v->cyc_peak)
        v->cyc_peak = c;
    /* Smoothed, not per block: Bungee alternates a lighter and a heavier block (one half-grain
     * each), so a raw per-block test never saw RB_GUARD_N in a row and the watchdog came first.
     * And the whole audio task, not only the leaf's share: the unit reset with Tools at 159 %. */
    if (v->cyc_avg <= RB_GUARD_CYC && diag_load() <= RB_GUARD_LOAD) {
        v->over_n = 0;
    } else if (++v->over_n >= RB_GUARD_N) {
        /* Off before the watchdog: the Clip plays stock grains from the next block. */
        v->state = (v->state & ~0xffu) | RB_ST_SLOW;
        label(v, RB_LABEL "! cpu");
    }
}

/* The expectations the build fills in (b2_finish in build_patch.py). In .rodata, so in the cave. */
__attribute__((used)) const volatile uint32_t rb_expect_id = 0xffffffffu;
__attribute__((used)) const volatile uint32_t rb_expect_sum = 0xffffffffu;
