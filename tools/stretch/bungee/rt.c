/* Bank 2 runtime for Bungee: its allocator (SRAM1-3 only), and the few OS calls newlib and
 * libstdc++ expect. Single-threaded: only the audio task runs bank 2 code after rubber_boot
 * (which runs once, from the disk task, before audio uses it).
 *
 * Bungee allocates everything when it is constructed (~0.2 MB with BUNGEE_FIXED_PITCH, measured on
 * the bench) and nothing while it plays, but unlike Rubber Band it frees two temporaries on the way
 * (the frequency-domain windows, 24 KB). So a bump allocator over two pools, plus a first-fit list
 * of freed blocks that later allocations reuse:
 *   1. the free part of SRAM1-3 after bank 2's .bss, up to the leaf's stack;
 *   2. the free part of ITCM after the hot code (bungee.ld .itcm), up to 0x10000. ITCM takes data
 *      at zero wait states too, and stock never touches it; its first 4 KB stay an empty guard.
 * Bungee's last arrays (the synthesis buffers) spill into ITCM. Nothing goes to SDRAM: the samples
 * keep all of it. A block that does not fit sets rb_heap_failed and returns 0; rubber_boot then
 * gives up ("Bungee! mem"). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

#define RB_FAST_END   0x30045000u     /* SRAM1-3 up to the leaf's stack (rubber.h RB_STACK_*) */
#define RB_ITCM_END   0x00010000u     /* ITCM, 64 KB */

extern char __b2_bss_end[], __itcm_end[];
static uint32_t fast_at, itcm_at;
uint32_t rb_fast_used;  /* bytes taken from both pools, including headers (bench) */
uint32_t rb_fast_peak;  /* SRAM1-3 bytes used past the heap start (bench) */
uint32_t rb_itcm_used;  /* ITCM bytes used past the code (bench) */
int rb_heap_failed;

typedef struct Head { uint32_t size; struct Head *next; } Head;  /* 8 bytes before every block */
static Head *freed;     /* freed blocks, first fit; size = usable bytes */

static void *take(size_t n)
{
    uint32_t need = ((uint32_t)n + 15u) & ~15u;
    Head **at, *h;
    for (at = &freed; *at; at = &(*at)->next)
        if ((*at)->size >= need) {
            h = *at;
            *at = h->next;
            return h + 1;
        }
    if (!fast_at) {
        fast_at = ((uint32_t)__b2_bss_end + 15u) & ~15u;
        itcm_at = ((uint32_t)__itcm_end + 15u) & ~15u;
    }
    if (fast_at + need + 16u <= RB_FAST_END) {
        h = (Head *)(fast_at + 16u);  /* the block 16-aligned, its header in the 8 bytes before */
        fast_at += need + 16u;
        rb_fast_peak = fast_at - (((uint32_t)__b2_bss_end + 15u) & ~15u);
    } else if (itcm_at + need + 16u <= RB_ITCM_END) {
        h = (Head *)(itcm_at + 16u);
        itcm_at += need + 16u;
        rb_itcm_used = itcm_at - (((uint32_t)__itcm_end + 15u) & ~15u);
    } else {
        rb_heap_failed = 1;
        errno = ENOMEM;
        return 0;
    }
    h[-1].size = need;
    rb_fast_used += need + 16u;
    return h;
}

/* The leaf rebuilds its stretcher when a launch changes the channel count (mono Clips run on one
 * channel). Everything Bungee allocates comes after this mark, so a rebuild rewinds the heap to
 * it instead of freeing piece by piece: no fragmentation, nothing left behind. */
static uint32_t mark_fast, mark_itcm, mark_used;
static Head *mark_freed;

void rb_heap_mark(void)
{
    if (!fast_at)
        (void)take(0);
    mark_fast = fast_at;
    mark_itcm = itcm_at;
    mark_used = rb_fast_used;
    mark_freed = freed;
}

void rb_heap_release(void)
{
    fast_at = mark_fast;
    itcm_at = mark_itcm;
    rb_fast_used = mark_used;
    freed = mark_freed;
}

void *malloc(size_t n) { return take(n); }
void free(void *p)
{
    Head *h;
    if (!p)
        return;
    h = (Head *)p - 1;
    h->next = freed;
    freed = h;
}
void *calloc(size_t a, size_t b)
{
    size_t n = a * b;
    char *p = take(n);
    if (p)
        for (size_t i = 0; i < n; i++)
            p[i] = 0;
    return p;
}
void *realloc(void *old, size_t n)
{
    char *p;
    size_t keep;
    if (!old)
        return take(n);
    keep = ((Head *)old)[-1].size;
    if (n <= keep)
        return old;
    p = take(n);
    if (p) {
        for (size_t i = 0; i < keep; i++)
            p[i] = ((char *)old)[i];
        free(old);
    }
    return p;
}
struct _reent;
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }

/* Nothing should still reach newlib's own heap; if something does, it fails rather than grow
 * into memory that is not ours. */
void *_sbrk(ptrdiff_t n)
{
    (void)n;
    errno = ENOMEM;
    return (void *)-1;
}

int _write(int f, const void *b, int n) { (void)f; (void)b; return n; }
int _read(int f, void *b, int n) { (void)f; (void)b; (void)n; return 0; }
int _close(int f) { (void)f; return -1; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return 0; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; errno = EINVAL; return -1; }
void _exit(int c) { (void)c; for (;;) __asm__ volatile("wfi"); }
int _gettimeofday(struct timeval *tv, void *tz) { (void)tz; if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; } return 0; }

void *__dso_handle = 0;
void _init(void) {}
void _fini(void) {}

/* 64-bit atomics: no other thread touches bank 2. */
uint64_t __atomic_load_8(const volatile void *p, int order) { (void)order; return *(const volatile uint64_t *)p; }
void __atomic_store_8(volatile void *p, uint64_t v, int order) { (void)order; *(volatile uint64_t *)p = v; }
_Bool __atomic_is_lock_free(unsigned long size, const volatile void *p) { (void)p; return size <= 4; }
