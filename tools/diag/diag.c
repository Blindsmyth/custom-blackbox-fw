/* Diagnostics for 3.1.R: the audio CPU meter on the Tools bar, and a crash recorder.
 *
 * Ported from blackbox-mod (reference/blackbox-mod/src/cpu.c and src/fault.c, MIT License,
 * Copyright (c) 2026 Justin Joe) and cut down to what this build needs: no USB, no bank 2 verdict
 * of theirs, no breadcrumb rings. The hardware facts are theirs, measured on a unit:
 *  - The audio task waits on a queue between 256-frame blocks (bl 0x08088756 at 0x080414E4).
 *    Busy = wake to wait, against the nominal block (the smallest per-second mean period since
 *    boot), so a late block reads high, not low. "+n" counts blocks later than 1.5 x nominal.
 *  - The Tools bar's middle label is empty (screen + 0xD4, the label before the free space). Ours
 *    is filled from the refresh's free-space query (bl 0x0809BBF8 at 0x080C5264), which runs every
 *    time; blackbox-mod hooked the label call after it, which stock skips when the query fails. Figures cover the time since the bar was last
 *    drawn. A version label with the figure appended never showed on the unit.
 *  - Stock's fault handlers spin until the watchdog resets, and every reset clears the SCB fault
 *    registers. So the handler records them itself, in backup SRAM, and resets at once.
 *  - Backup SRAM is write-back cached (no MPU region): a record must be cleaned out of the cache
 *    before the reset, or the reset throws it away.
 *  - The window watchdog's early wakeup (IRQ 0) fires one tick before its reset; stock's HAL
 *    leaves it off and clears the enable after us, so it is re-armed from the audio wait.
 *
 * What shows on the Tools bar's middle label:
 *   "34/71%" or "34/71%+3"   load since the last look (avg / peak, late blocks)
 *   "F0810abcd"              the boot before died in a fault at that PC (this boot only)
 *   "W0810abcd"              the boot before was reset by the watchdog while running that PC
 * The Rubber label in the Warp list carries the stage the boot before died in (b2pair.c).
 *
 * Bank 1 code: no .data/.bss. State lives in SRAM4 (DIAG) and backup SRAM (b2.h RB_BKP_*). */
#include "b2.h"

#define FN(a) ((a) | 1u)
typedef int (*receive_fn)(void *queue, void *item, uint32_t ticks, int peek);
typedef void (*label_fn)(uint8_t *label, const char *s);
#define fw_receive   ((receive_fn)FN(0x08088756u))   /* FreeRTOS queue receive */
#define fw_label_set ((label_fn)FN(0x080a3decu))     /* label: set its text, then redraw */

#define DEMCR      (*(volatile uint32_t *)0xe000edfcu)
#define DWT_CTRL   (*(volatile uint32_t *)0xe0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xe0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xe0001fb0u)
#define CFSR       (*(volatile uint32_t *)0xe000ed28u)
#define HFSR       (*(volatile uint32_t *)0xe000ed2cu)
#define MMFAR      (*(volatile uint32_t *)0xe000ed34u)
#define BFAR       (*(volatile uint32_t *)0xe000ed38u)
#define AIRCR      (*(volatile uint32_t *)0xe000ed0cu)
#define WWDG_CFR   (*(volatile uint32_t *)0x50003004u)
#define WWDG_SR    (*(volatile uint32_t *)0x50003008u)
#define WWDG_EWI   (1u << 9)
#define WDG_SUP    ((volatile uint32_t *)0x2400a584u) /* +0 stamp A, +4 stamp B, +8 the supervisor's counter */
#define NVIC_ISER0 (*(volatile uint32_t *)0xe000e100u)
#define NVIC_IPR0  (*(volatile uint8_t *)0xe000e400u) /* IRQ 0 = WWDG1 */

#define BLOCKS_PER_REPORT 188u   /* ~1 s of 5.33 ms blocks */
#define PERIOD_FLOOR 100000u     /* a real block is far longer; two queued interrupts are not one */

struct diag_state {
    uint32_t magic;
    uint32_t woke;               /* cycle count when the task last woke */
    uint32_t sum;                /* per-mille loads since the last look */
    uint32_t nom;                /* nominal block, cycles */
    uint32_t win_start;
    uint16_t n, peak;
    uint16_t win_n, wakes;
    uint16_t avg_shown, peak_shown;
    uint8_t armed, status, late, pad;
};
#define DIAG       ((volatile struct diag_state *)0x38007100u)   /* SRAM4, after RbVoice */
#define DIAG_MAGIC 0x31474144u   /* "DAG1" */
#define ST_NONE     0
#define ST_OK       1
#define ST_NO_CLOCK 2

#define REC(at)    ((volatile uint32_t *)(at))

static void start(void)
{
    volatile struct diag_state *m = DIAG;
    DEMCR |= 1u << 24;           /* TRCENA */
    DWT_LAR = 0xc5acce55u;       /* unlock (Cortex-M7) */
    DWT_CTRL |= 1u;              /* CYCCNTENA */
    m->armed = 0;
    m->status = ST_NONE;
    m->sum = 0;
    m->nom = 0;
    m->win_start = DWT_CYCCNT;
    m->late = 0;
    m->n = m->peak = m->win_n = m->wakes = 0;
    m->avg_shown = m->peak_shown = 0;
    m->magic = DIAG_MAGIC;
}

/* Replaces the audio task's queue wait (bl 0x08088756 at 0x080414E4). */
int cpu_wait(void *queue, void *item, uint32_t ticks, int peek)
{
    volatile struct diag_state *m = DIAG;
    uint32_t idle_from, busy, now, period, base, load, mean;
    int got;
    if (m->magic != DIAG_MAGIC)
        start();
    if (!(WWDG_CFR & WWDG_EWI))
        WWDG_CFR |= WWDG_EWI;    /* stock's HAL_WWDG_Init clears it after diag_boot armed it */
    idle_from = DWT_CYCCNT;
    busy = idle_from - m->woke;
    got = fw_receive(queue, item, ticks, peek);
    now = DWT_CYCCNT;
    period = now - m->woke;
    if (got != 1) {
        m->armed = 0;
        return got;
    }
    if (m->armed && period > 1000u) {
        base = m->nom ? m->nom : period;
        if (m->nom && period > base + (base >> 1) && m->late < 99u)
            m->late++;
        load = busy / (base / 1000u + 1u);
        if (load > 9990u)
            load = 9990u;
        if (m->n >= 60000u) {    /* ~5 minutes without a look: halve rather than wrap */
            m->sum >>= 1;
            m->n >>= 1;
        }
        m->sum += load;
        m->n++;
        if (load > m->peak)
            m->peak = (uint16_t)load;
        if (++m->win_n >= BLOCKS_PER_REPORT) {
            m->status = ST_OK;
            mean = (now - m->win_start) / m->win_n;
            if (mean > PERIOD_FLOOR && (m->nom == 0 || mean < m->nom))
                m->nom = mean;
            m->win_start = now;
            m->win_n = 0;
        }
    }
    if (++m->wakes >= 2u * BLOCKS_PER_REPORT) {
        if (m->n == 0)
            m->status = ST_NO_CLOCK;
        m->wakes = 0;
    }
    m->woke = now;
    m->armed = 1;
    return got;
}

static char *put_uint(char *p, unsigned v)
{
    char tmp[5];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < 5);
    while (n)
        *p++ = tmp[--n];
    return p;
}

static char *put_hex(char *p, uint32_t v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> i) & 15u;
        *p++ = (char)(d < 10u ? '0' + d : 'a' + d - 10u);
    }
    return p;
}

static void compose(char *buf)
{
    volatile struct diag_state *m = DIAG;
    char *p = buf;
    if (*REC(RB_BKP_FAULT_SV) == RB_FAULT_MAGIC) {
        *p++ = 'F';
        p = put_hex(p, REC(RB_BKP_FAULT_SV)[1]);
    } else if (*REC(RB_BKP_WDG_SV) == RB_WDG_MAGIC) {
        *p++ = 'W';
        p = put_hex(p, REC(RB_BKP_WDG_SV)[1]);
    } else if (m->magic == DIAG_MAGIC && m->status == ST_OK) {
        p = put_uint(p, (m->avg_shown + 5u) / 10u);
        *p++ = '/';
        p = put_uint(p, (m->peak_shown + 5u) / 10u);
        *p++ = '%';
        if (m->late) {
            *p++ = '+';
            p = put_uint(p, m->late);
        }
    } else if (m->magic == DIAG_MAGIC && m->status == ST_NO_CLOCK) {
        *p++ = '-';
        *p++ = '-';
        *p++ = '%';
    }
    *p = 0;
}

/* The Tools refresh (FUN_080C5180) asks for the card's free space at 0x080C5264 before it sets
 * that label, and sets it only when the query succeeds; tools_thunk (rubber_cave.S) comes here
 * first with the bar's empty middle label (screen + 0xD4, one label before the free space at
 * +0x174). Screen task. */
void cpu_mid(uint8_t *label)
{
    volatile struct diag_state *m = DIAG;
    char buf[16];
    if (m->magic == DIAG_MAGIC && m->n) {
        m->avg_shown = (uint16_t)(m->sum / m->n);
        m->peak_shown = m->peak;
        m->sum = 0;
        m->n = m->peak = 0;
    }
    compose(buf);
    if (m->magic == DIAG_MAGIC)
        m->late = 0;
    fw_label_set(label, buf);
}

/* Is a stacked frame pointer safe to read? After a stacking fault it points wherever SP went. */
static int frame_ok(uint32_t sp)
{
    if (sp & 7u)
        return 0;
    return (sp >= 0x20000000u && sp + 0x20u <= 0x20020000u) ||
           (sp >= 0x24000000u && sp + 0x20u <= 0x24080000u) ||
           (sp >= 0x30000000u && sp + 0x20u <= 0x30048000u) ||
           (sp >= 0x38000000u && sp + 0x20u <= 0x38010000u);
}

void fault_record(uint32_t *frame, uint32_t exc_return)
{
    volatile uint32_t *r = REC(RB_BKP_FAULT);
    uint32_t sp = (uint32_t)frame;
    int ok = frame_ok(sp);
    rb_bkp_enable();
    r[4] = CFSR;                 /* from the core first: these land even if the frame does not */
    r[5] = HFSR;
    r[6] = MMFAR;
    r[7] = BFAR;
    r[8] = exc_return;
    r[9] = sp;
    r[10] = (uint32_t)ok;
    r[11] = *REC(RB_BKP_STAGE);
    r[1] = r[2] = r[3] = 0;
    r[0] = RB_FAULT_MAGIC;
    if (ok) {
        r[1] = frame[6];         /* PC */
        r[2] = frame[5];         /* LR */
        r[3] = frame[7];         /* xPSR */
    }
    rb_bkp_flush();
    AIRCR = (0x5fau << 16) | (1u << 2);   /* SYSRESETREQ */
    for (;;)
        ;
}

/* HardFault, MemManage, BusFault, UsageFault. Naked: the frame is the stack as it was. */
__attribute__((naked)) void fault_handler(void)
{
    __asm__ volatile(
        "tst   lr, #4            \n"
        "ite   eq                \n"
        "mrseq r0, msp           \n"
        "mrsne r0, psp           \n"
        "mov   r1, lr            \n"
        "b     fault_record      \n");
}

/* The watchdog's early wakeup, one tick before its reset: note where the CPU was and which task
 * stopped checking in. The reset still happens. */
void wdg_record(uint32_t *frame, uint32_t exc_return)
{
    volatile uint32_t *r = REC(RB_BKP_WDG);
    uint32_t now = WDG_SUP[2];
    (void)exc_return;
    rb_bkp_enable();
    r[1] = frame_ok((uint32_t)frame) ? frame[6] : 0;
    r[2] = frame_ok((uint32_t)frame) ? frame[5] : 0;
    r[3] = frame_ok((uint32_t)frame) ? frame[7] : 0;
    r[4] = now - WDG_SUP[0];     /* over 3,749: the task that stopped */
    r[5] = now - WDG_SUP[1];
    r[6] = now;
    r[7] = *REC(RB_BKP_STAGE);
    r[0] = RB_WDG_MAGIC;
    rb_bkp_flush();
    WWDG_SR = 0;                 /* EWIF: once, not in a loop */
}

__attribute__((naked)) void wdg_handler(void)
{
    __asm__ volatile(
        "tst   lr, #4            \n"
        "ite   eq                \n"
        "mrseq r0, msp           \n"
        "mrsne r0, psp           \n"
        "mov   r1, lr            \n"
        "b     wdg_record        \n");
}

static void latch(uint32_t live, uint32_t saved, uint32_t words, uint32_t magic)
{
    volatile uint32_t *l = REC(live), *s = REC(saved);
    uint32_t i;
    if (l[0] == magic) {
        for (i = 0; i < words; i++)
            s[i] = l[i];
        l[0] = 0;
    } else {
        s[0] = 0;                /* shown for the one boot after the crash only */
    }
}

/* Once per boot, from rb_pair (backup SRAM already enabled). */
void diag_boot(void)
{
    DIAG->magic = 0;             /* SRAM4 survives a reset: the meter starts again */
    latch(RB_BKP_FAULT, RB_BKP_FAULT_SV, 12u, RB_FAULT_MAGIC);
    latch(RB_BKP_WDG, RB_BKP_WDG_SV, 8u, RB_WDG_MAGIC);
    rb_bkp_flush();
    NVIC_IPR0 = 0;               /* highest: it must preempt whatever is spinning */
    NVIC_ISER0 = 1u;             /* IRQ 0 = WWDG1 (write-one-to-set) */
    WWDG_CFR |= WWDG_EWI;
}
