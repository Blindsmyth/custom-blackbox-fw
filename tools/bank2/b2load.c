/* The card loader: programs flash bank 2 from BBRUBBER.BIN in the card root, once, when pairing
 * found bank 2 is not the payload this image was built with. Stock's SD installer only writes
 * bank 1, so this is how the Rubber engine gets onto the unit.
 *
 * Where it runs (facts from blackbox-mod's src/b2load.c, which proved them on hardware):
 *  - Only in pcmStreamer, the task that owns FatFs (stock's FatFs takes no lock): its loop
 *    0x080743B0 calls the file-command step 0x08092F5C from 0x0807443E, 0x0807446E and
 *    0x080744D8, and each of those `bl` comes through b2load_thunk first.
 *  - The window watchdog needs this task's stamp (0x2400A584 <- 0x2400A58C) within 10 s, so long
 *    loops stamp.
 *  - pcmStreamer runs at priority 0, below the audio task, so waiting for an erase (~0.9 s per
 *    sector) costs the audio nothing.
 * What it may write: bank 2 through KEYR2/CR2/SR2/CCR2 only, sectors 0-5, plus the claim word at
 * the start of sector 6. Never bank 1's registers, never sector 7. See b2.h for the other rules. */
#include "b2.h"

#define FN(a) ((a) | 1u)
typedef int (*f_open_fn)(void *file, const char *path, unsigned mode);
typedef int (*f_read_fn)(void *file, void *to, unsigned bytes, unsigned *got);
typedef int (*f_close_fn)(void *file);
#define fw_f_open  ((f_open_fn)FN(0x08086534u))
#define fw_f_read  ((f_read_fn)FN(0x08086866u))
#define fw_f_close ((f_close_fn)FN(0x08086d76u))
#define FIL_BYTES  0x58u   /* stock's FIL, cleared before every f_open */
#define FA_READ    1u

#define CARD_FOUND (*(const volatile uint8_t *)(0x240021a4u + 0x15u))  /* stock: root opened */
#define WDG_DISK   (*(volatile uint32_t *)0x2400a584u)
#define WDG_NOW    (*(const volatile uint32_t *)0x2400a58cu)

#define CR_LOCK    (1u << 0)
#define CR_PG      (1u << 1)
#define CR_SER     (1u << 2)
#define CR_PSIZE64 (3u << 4)
#define CR_START   (1u << 7)
#define SR_BUSY    ((1u << 0) | (1u << 1) | (1u << 2))
#define SR_EOP     (1u << 16)
#define SR_ERRORS  ((1u << 17) | (1u << 18) | (1u << 19) | (1u << 21) | (1u << 22) | (1u << 23) | (1u << 24))
#define KEY1       0x45670123u
#define KEY2       0xcdef89abu
#define DCIMVAC    (*(volatile uint32_t *)0xe000ef5cu)
#define ICIALLU    (*(volatile uint32_t *)0xe000ef50u)
#define AIRCR      (*(volatile uint32_t *)0xe000ed0cu)

/* Loop counts, not times: an erase took 0.87 s on blackbox-mod's unit. */
#define SPINS_ERASE 400000000u
#define SPINS_WORD  20000000u

/* RbVoice.state bit: the loader has had its one go this boot. */
#define RB_ST_LOOKED 0x100u

static const char RB_FILE[] = "BBRUBBER.BIN";
/* Opt-in files in the card root. Bank 2 holding someone else's payload (blackbox-mod uses the
 * same sectors, with its own claim word "BB2C") is left alone unless RB_TAKE is there; RB_FREE
 * erases ours and leaves bank 2 blank, which blackbox-mod's loader accepts again. */
static const char RB_TAKE[] = "RBTAKE.TXT";
static const char RB_FREE[] = "RBFREE.TXT";
static const uint32_t claim_word[8] = { RB_CLAIM, ~RB_CLAIM, 1u, 0, 0, 0, 0, 0 };

static void stamp(void)
{
    WDG_DISK = WDG_NOW;
}

typedef struct { uint32_t fil[FIL_BYTES / 4]; } Src;

static int open_named(Src *s, const char *name)
{
    unsigned i;
    for (i = 0; i < FIL_BYTES / 4; i++)
        s->fil[i] = 0;
    return fw_f_open(s->fil, name, FA_READ) == 0;
}

static int src_open(Src *s)
{
    return open_named(s, RB_FILE);
}

static int file_there(const char *name)
{
    Src s;
    if (!open_named(&s, name))
        return 0;
    fw_f_close(s.fil);
    return 1;
}

static int src_word(Src *s, uint32_t w[8])
{
    unsigned got = 0;
    return fw_f_read(s->fil, w, 32u, &got) == 0 && got == 32u;
}

static int header_ok(const uint32_t w[8])
{
    return w[0] == RB_MAGIC && w[1] == rb_expect_id && w[2] >= 64u && w[2] <= RB_B2_LIMIT &&
           !(w[2] & 31u) && w[3] == rb_expect_sum;
}

/* The whole file is read and summed before anything is erased. 1 = this image's payload,
 * 0 = no file, -1 = a file, but another build's or damaged. */
static int file_ok(void)
{
    Src s;
    uint32_t w[8], len, sum, at;
    int ok = 0, i;
    if (!src_open(&s))
        return 0;
    rb_stage(RB_SG_FILE);
    if (src_word(&s, w) && header_ok(w)) {
        len = w[2];
        sum = w[4] + w[5] + w[6] + w[7];
        for (at = 32u; at < len && src_word(&s, w); at += 32u) {
            for (i = 0; i < 8; i++)
                sum += w[i];
            if (!(at & 0xffffu))
                stamp();
        }
        ok = at >= len && sum == rb_expect_sum;
    }
    fw_f_close(s.fil);
    rb_stage(0);
    return ok ? 1 : -1;
}

static inline void barrier(void)
{
    __asm__ volatile("isb\n dsb");
}

static int flash_wait(uint32_t spins)
{
    while (FLASH_SR2 & SR_BUSY)
        if (!spins--)
            return 0;
    return !(FLASH_SR2 & SR_ERRORS);
}

static int flash_unlock(void)
{
    if (FLASH_CR2 & CR_LOCK) {
        FLASH_KEYR2 = KEY1;
        FLASH_KEYR2 = KEY2;
    }
    if (FLASH_CR2 & CR_LOCK)
        return 0;
    FLASH_CCR2 = SR_ERRORS | FLASH_ECC | SR_EOP;
    return 1;
}

static void flash_lock(void)
{
    FLASH_CR2 = CR_PSIZE64 | CR_LOCK;
}

/* Sectors 0-5; sector 6 (the claim word) only when claim_too says so: taking bank 2 over or
 * giving it back. Sector 7, factory calibration, never. */
static int flash_erase_in(unsigned sector, int claim_too);

static int flash_erase(unsigned sector)
{
    return flash_erase_in(sector, 0);
}

static int flash_erase_in(unsigned sector, int claim_too)
{
    int ok;
    unsigned limit = (claim_too ? RB_B2_END : RB_B2_LIMIT) / RB_B2_SECTOR;
    if (sector >= limit || sector >= 7u || !flash_wait(SPINS_WORD))
        return 0;
    FLASH_CR2 = CR_PSIZE64 | CR_SER | (sector << 8);
    FLASH_CR2 = CR_PSIZE64 | CR_SER | (sector << 8) | CR_START;
    barrier();
    ok = flash_wait(SPINS_ERASE);
    FLASH_CR2 = CR_PSIZE64;
    return ok;
}

/* One 256-bit flash word: eight word writes start the programming. */
static int flash_word(uint32_t at, const uint32_t w[8])
{
    volatile uint32_t *to;
    int i, ok;
    if (at >= RB_B2_END || (at & 31u) || (at >= RB_B2_LIMIT && at != RB_B2_CLAIM_AT))
        return 0;
    to = (volatile uint32_t *)(RB_B2_BASE + at);
    FLASH_CR2 = CR_PSIZE64 | CR_PG;
    barrier();
    for (i = 0; i < 8; i++)
        to[i] = w[i];
    barrier();
    ok = flash_wait(SPINS_WORD);
    FLASH_CR2 = CR_PSIZE64;
    return ok;
}

static int claimed(void)
{
    DCIMVAC = RB_B2_BASE + RB_B2_CLAIM_AT;
    return *(const volatile uint32_t *)(RB_B2_BASE + RB_B2_CLAIM_AT) == RB_CLAIM;
}

/* Sectors 0-6 all erased? (Read under the scan mark: a bad word faults.) */
static int virgin(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)RB_B2_BASE;
    uint32_t i;
    for (i = 0; i < RB_B2_END / 4u; i++) {
        if (p[i] != 0xffffffffu)
            return 0;
        if (!(i & 0x3fffu))
            stamp();
    }
    return 1;
}

/* Erase sectors 0-6: everything a payload and a claim word can be in. */
static int erase_all(void)
{
    unsigned sector;
    int ok = flash_unlock();
    for (sector = 0; ok && sector * RB_B2_SECTOR < RB_B2_END; sector++) {
        rb_stage(RB_SG_ERASE + sector);
        ok = flash_erase_in(sector, 1);
        stamp();
    }
    flash_lock();
    for (sector = 0; sector < RB_B2_END; sector += 32u)
        DCIMVAC = RB_B2_BASE + sector;
    rb_stage(0);
    return ok;
}

#define OWN_NOT   0   /* someone else's: left alone */
#define OWN_OURS  1   /* our claim */
#define OWN_BLANK 2   /* erased: claim it */
#define OWN_TAKE  3   /* someone else's, and RBTAKE.TXT says take it */

/* Whose is bank 2? Reads only (under the scan mark: a bad word faults). */
static int probe(void)
{
    int own;
    rb_bkp_put(RB_BKP_MARK, RB_MARK_SCAN);
    rb_stage(RB_SG_CLAIM);
    own = claimed() ? OWN_OURS : virgin() ? OWN_BLANK : OWN_NOT;
    rb_bkp_put(RB_BKP_MARK, 0);
    rb_stage(0);
    if (own == OWN_NOT && file_there(RB_TAKE))
        own = OWN_TAKE;
    return own;
}

/* Make bank 2 ours: erase it all first when taking it over, then write the claim word. */
static int claim(int own)
{
    int ok;
    if (own == OWN_OURS)
        return 1;
    if (own == OWN_TAKE && !erase_all())
        return 0;
    ok = flash_unlock() && flash_word(RB_B2_CLAIM_AT, claim_word);
    flash_lock();
    return ok && claimed();
}

/* RBFREE.TXT: give bank 2 back. Only ours is erased; the voice stops first. pcmStreamer runs
 * below the audio task, so no render is under way while this runs, and the next block sees
 * Rubber off. Returns 1 if the file is there (then nothing else is written this boot). */
static int release(RbVoice *v)
{
    if (!file_there(RB_FREE))
        return 0;
    if (!claimed()) {
        rb_note('f');                     /* already not ours */
        return 1;
    }
    v->state = (v->state & ~0xffu) | RB_ST_ABSENT;
    __asm__ volatile("dsb");
    rb_note(erase_all() && virgin() ? 'f' : 'e');
    return 1;
}

static int program(void)
{
    Src s;
    uint32_t first[8], w[8], len, at, a;
    unsigned sector;
    int ok;
    if (!src_open(&s))
        return 0;
    ok = src_word(&s, first) && header_ok(first) && flash_unlock();
    len = first[2];
    for (sector = 0; ok && sector * RB_B2_SECTOR < len; sector++) {
        rb_stage(RB_SG_ERASE + sector);
        ok = flash_erase(sector);
        stamp();
    }
    rb_stage(RB_SG_WRITE);
    for (at = 32u; ok && at < len; at += 32u) {
        ok = src_word(&s, w) && flash_word(at, w);
        if (!(at & 0x1fffu))
            stamp();
    }
    if (ok)
        ok = flash_word(0, first);   /* the header last: it is what makes the payload valid */
    flash_lock();
    fw_f_close(s.fil);
    for (a = RB_B2_BASE; a < RB_B2_BASE + len; a += 32u)
        DCIMVAC = a;                  /* caches may still hold the old bank 2 */
    ICIALLU = 0;
    __asm__ volatile("dsb\n isb");
    stamp();
    rb_stage(0);
    return ok;
}

static uint32_t tries_key(void)
{
    return rb_expect_id ^ rb_expect_sum ^ 0x54524952u;
}

/* b2load_thunk calls this from pcmStreamer before each file-command step. It also starts a
 * paired bank 2 (rb_start): here the unit is fully up, and this task may take its time. */
void rb_load_poll(void)
{
    RbVoice *v = (RbVoice *)RB_VOICE;
    uint32_t n;
    int own;
    if (v->magic != RB_VOICE_MAGIC)
        return;
    if ((v->state & 0xffu) == RB_ST_PAIRED) {
        rb_start();
        return;
    }
    if ((v->state & RB_ST_LOOKED) || CARD_FOUND != 1u)
        return;
    if ((v->state & 0xffu) == RB_ST_UNKNOWN)
        return;                       /* pairing has not run yet: ask again next step */
    v->state |= RB_ST_LOOKED;         /* before anything else: one go per boot */
    if (release(v))
        return;
    if ((v->state & 0xffu) == RB_ST_ABSENT)
        rb_note('.');                     /* looked; replaced by the verdict below */
    /* Only a bank 2 without this image's payload is written. READY, and BROKEN (intact, but its
     * start crashed), are left alone: rewriting the same payload cannot help, and clearing the
     * boot mark with it is what turned one crash into a crash on every preset load. */
    if ((v->state & 0xffu) != RB_ST_ABSENT)
        return;
    /* From here every way out names itself after the Rubber label (rb_note): "Rubber! -". */
    n = (uint32_t)file_ok();
    if (n != 1u) {
        rb_note(n ? '?' : '-');           /* '-' no BBRUBBER.BIN, '?' another build's or damaged */
        return;
    }
    own = probe();
    if (own == OWN_NOT) {
        rb_note('x');                     /* someone else's payload (RBTAKE.TXT to replace it) */
        return;
    }
    n = rb_bkp_get(RB_BKP_TRIES_ID) == tries_key() ? rb_bkp_get(RB_BKP_TRIES_N) : 0u;
    if (n >= RB_MAX_TRIES) {
        rb_note('t');                     /* two writes of this payload failed already */
        return;
    }
    rb_bkp_put(RB_BKP_TRIES_ID, tries_key());
    rb_bkp_put(RB_BKP_TRIES_N, n + 1u);   /* before the write: one that kills the boot counts */
    if (!claim(own)) {
        rb_note('e');
        return;
    }
    if (!program()) {
        rb_note('e');                     /* the flash reported an error */
        return;
    }
    rb_bkp_put(RB_BKP_MARK, RB_MARK_SCAN);
    rb_stage(RB_SG_VERIFY);
    n = (uint32_t)rb_b2_intact();
    rb_bkp_put(RB_BKP_MARK, 0);
    rb_stage(0);
    if (!n) {
        rb_note('v');                     /* written, but did not read back right */
        return;
    }
    rb_bkp_put(RB_BKP_TRIES_N, 0);
    rb_bkp_put(RB_BKP_BOOTING, 0);        /* a new payload gets a fresh boot */
    rb_stage(0);
    rb_bkp_flush();
    __asm__ volatile("dsb");
    AIRCR = (0x5fau << 16) | (1u << 2);   /* SYSRESETREQ: boot again with bank 2 in place */
    for (;;)
        ;
}
