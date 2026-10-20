/*
 * Audio CPU meter (Tools middle label) + flash bank-2 pairing for RB Warp.
 * Port of blackbox-mod src/cpu.c pairing/meter, without USB device / master titles.
 */
#include <stdint.h>
#include "b2.h"

#ifndef B2_ID
#define B2_ID 0x52425731u
#endif

#define FN(addr) ((addr) | 1u)

typedef int (*receive_fn)(void *queue, void *item, uint32_t ticks, int peek);
typedef void (*label_fn)(uint8_t *label, const char *s);

#define fw_receive   ((receive_fn)FN(0x08088756))
#define fw_label_set ((label_fn)FN(0x080a3dec))

#define LABEL_SIZE  0xa0

#define DEMCR      (*(volatile uint32_t *)0xe000edfcu)
#define DWT_CTRL   (*(volatile uint32_t *)0xe0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xe0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xe0001fb0u)

#define BLOCKS_PER_REPORT 188
#define PERIOD_FLOOR 100000u

struct cpu_state {
    uint32_t magic;
    uint32_t woke;
    uint32_t sum;
    uint32_t nom;
    uint32_t win_start;
    uint16_t n, peak;
    uint16_t wakes;
    uint16_t win_n;
    uint16_t avg_shown, peak_shown;
    uint8_t armed, status;
    uint8_t late;
    uint8_t b2chk;
};
#define M ((volatile struct cpu_state *)0x2405ffd0u)
#define MAGIC 0x43505534u /* "CPU4" */
#define STATUS_NONE     0
#define STATUS_OK       1
#define STATUS_NO_CLOCK 2

#define FLASH_SR2   (*(volatile uint32_t *)0x52002110u)
#define FLASH_CCR2  (*(volatile uint32_t *)0x52002114u)
#define FLASH_ECC   ((1u << 25) | (1u << 26))
#define DCIMVAC     (*(volatile uint32_t *)0xe000ef5cu)
#define B2_SCANS    16
#define B2_BYTE     ((uint8_t *)&M->b2chk)
#define B2_SUM_ON_UNIT (*(const volatile uint32_t *)0x0810000cu)

__attribute__((used)) const volatile uint32_t b2_expect_sum = 0xffffffffu;

void bkp_enable(void)
{
    *(volatile uint32_t *)0x58024800u |= (1u << 28);
    *(volatile uint32_t *)0x58024400u |= (1u << 8);
    __asm__ volatile("dsb\n isb");
}

int b2_ok(void)
{
    const volatile uint32_t *h = (const volatile uint32_t *)B2_BASE;
    if (h[0] != B2_MAGIC || h[1] != (uint32_t)B2_ID)
        return 0;
    uint32_t len = h[2];
    if (len < 32u || len > B2_LIMIT || (len & 31u))
        return 0;
    uint32_t chk = 0;
    const volatile uint32_t *p = (const volatile uint32_t *)(B2_BASE + 16u);
    for (uint32_t i = 16u; i < len; i += 4u)
        chk += *p++;
    return chk == h[3];
}

static void start(void)
{
    DEMCR |= 1u << 24;
    DWT_LAR = 0xc5acce55u;
    DWT_CTRL |= 1u;
    M->armed = 0;
    M->status = STATUS_NONE;
    M->sum = 0;
    M->nom = 0;
    M->win_start = DWT_CYCCNT;
    M->late = 0;
    M->b2chk = 0;
    M->n = M->peak = M->wakes = M->win_n = 0;
    M->avg_shown = M->peak_shown = 0;
    M->magic = MAGIC;
}

void cpu_boot(void)
{
    M->magic = 0;
    /* Bungee leaf's SDRAM arena record (bg_fw.c): the stock bump allocator restarts on a
     * soft reset, so a pointer from before it would alias live firmware memory. */
    *(volatile uint32_t *)0x38005200u = 0;
}

static char *put_uint(char *p, unsigned v)
{
    char tmp[4];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 4);
    while (n)
        *p++ = tmp[--n];
    return p;
}

static int b2_paired(void)
{
    if (__atomic_fetch_or(B2_BYTE, B2_SCANNING, __ATOMIC_SEQ_CST) & B2_SCANNING)
        return -1;
    int ok = 0;
    bkp_enable();
    uint32_t mark = *B2_MARK;
    if (mark == B2_MARK_NAMING || mark == B2_MARK_CLAIMING) {
        ok = -1;
    } else if (mark != B2_MARK_SCANNING) {
        bkp_put(B2_MARK, B2_MARK_SCANNING);
        if (b2_ok() && B2_SUM_ON_UNIT == b2_expect_sum) {
            uint32_t len = *(const volatile uint32_t *)(B2_BASE + 8u);
            FLASH_CCR2 = FLASH_ECC;
            for (int pass = 0; pass < B2_SCANS; pass++)
                for (uint32_t a = B2_BASE; a < B2_BASE + len; a += 32u) {
                    DCIMVAC = a;
                    (void)*(const volatile uint32_t *)a;
                }
            ok = !(FLASH_SR2 & FLASH_ECC);
        }
        bkp_put(B2_MARK, 0);
    }
    __atomic_and_fetch(B2_BYTE, (uint8_t)~B2_SCANNING, __ATOMIC_SEQ_CST);
    return ok;
}

uint8_t b2_state(void)
{
    return M->magic == MAGIC ? M->b2chk : 0u;
}

void b2_flag(uint8_t bits)
{
    __atomic_or_fetch(B2_BYTE, bits, __ATOMIC_SEQ_CST);
}

int b2_ready(void)
{
    return M->magic == MAGIC;
}

static uint8_t b2_checked(void)
{
    if (!(M->b2chk & B2_PAIR)) {
        int paired = b2_paired();
        if (paired >= 0)
            b2_flag(paired ? B2_PAIRED : B2_UNPAIRED);
    }
    return M->b2chk & B2_PAIR;
}

int b2_live(void)
{
    if (M->magic != MAGIC)
        return 0;
    return b2_checked() == B2_PAIRED;
}

int b2_present(void)
{
    bkp_enable();
    uint32_t mark = *B2_MARK;
    if (mark == B2_MARK_SCANNING || mark == B2_MARK_NAMING || mark == B2_MARK_CLAIMING)
        return 0;
    bkp_put(B2_MARK, B2_MARK_NAMING);
    int ok = b2_ok() && B2_SUM_ON_UNIT == b2_expect_sum;
    bkp_put(B2_MARK, 0);
    return ok;
}

int b2_rescan(void)
{
    bkp_enable();
    bkp_put(B2_MARK, 0);
    int ok = b2_paired() > 0;
    if (ok) {
        __atomic_and_fetch(B2_BYTE, (uint8_t)~B2_PAIR, __ATOMIC_SEQ_CST);
        b2_flag(B2_PAIRED);
    }
    return ok;
}

static void compose(char *buf)
{
    char *p = buf;
    if (M->magic == MAGIC && b2_checked() != B2_PAIRED)
        *p++ = '!';
    if (M->magic == MAGIC && M->status == STATUS_OK) {
        p = put_uint(p, (M->avg_shown + 5u) / 10u);
        *p++ = '/';
        p = put_uint(p, (M->peak_shown + 5u) / 10u);
        *p++ = '%';
        if (M->late) {
            *p++ = '+';
            p = put_uint(p, M->late);
        }
    } else if (M->magic == MAGIC && M->status == STATUS_NO_CLOCK) {
        *p++ = '-';
        *p++ = '-';
        *p++ = '%';
    } else if (M->magic == MAGIC) {
        /* paired marker / loader file verdict while figures are not ready yet */
        static const char verdict[4] = {0, '+', '-', '?'};
        char v = verdict[(M->b2chk >> B2_FILE_SHIFT) & 3u];
        if (v)
            *p++ = v;
    }
    *p = 0;
}

int cpu_wait(void *queue, void *item, uint32_t ticks, int peek)
{
    if (M->magic != MAGIC)
        start();
    uint32_t idle_from = DWT_CYCCNT, busy = idle_from - M->woke;
    int got = fw_receive(queue, item, ticks, peek);
    uint32_t now = DWT_CYCCNT, period = now - M->woke;
    if (got != 1) {
        M->armed = 0;
        return got;
    }
    if (M->armed && period > 1000u) {
        uint32_t base = M->nom ? M->nom : period;
        if (M->nom && period > base + (base >> 1) && M->late < 99u)
            M->late++;
        uint32_t load = busy / (base / 1000u + 1u);
        if (load > 9990u)
            load = 9990u;
        if (M->n >= 60000u) {
            M->sum >>= 1;
            M->n >>= 1;
        }
        M->sum += load;
        M->n++;
        if (load > M->peak)
            M->peak = (uint16_t)load;
        if (++M->win_n >= BLOCKS_PER_REPORT) {
            M->status = STATUS_OK;
            uint32_t mean = (now - M->win_start) / M->win_n;
            if (mean > PERIOD_FLOOR && (M->nom == 0 || mean < M->nom))
                M->nom = mean;
            M->win_start = now;
            M->win_n = 0;
        }
    }
    if (++M->wakes >= 2 * BLOCKS_PER_REPORT) {
        if (M->n == 0)
            M->status = STATUS_NO_CLOCK;
        M->wakes = 0;
    }
    M->woke = now;
    M->armed = 1;
    return got;
}

void cpu_tools(uint8_t *space_label, const char *space)
{
    char buf[16];
    fw_label_set(space_label, space);
    if (M->n) {
        M->avg_shown = (uint16_t)(M->sum / M->n);
        M->peak_shown = M->peak;
        M->sum = 0;
        M->n = M->peak = 0;
    }
    compose(buf);
    M->late = 0;
    fw_label_set(space_label - LABEL_SIZE, buf);
}
