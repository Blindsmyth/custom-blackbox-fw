/* Bank 2 runtime for Rubber Band: its allocator (SRAM1, then the top megabyte of SDRAM), and the
 * few OS calls newlib and libstdc++ expect. Single-threaded: only the audio task runs bank 2 code
 * after rubber_boot (which runs once, before audio uses it).
 *
 * The SDRAM reserve exists because the build lowers stock's sample allocator top from
 * 0xC4000000 to RB_HEAP_START (the .data initialiser at 0x080F1E08 and the empty-stack reset in
 * FUN_08044470), so samples never reach it. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

#define RB_HEAP_START 0xC3F00000u
#define RB_HEAP_END   0xC4000000u
#define RB_FAST_END   0x30040000u     /* SRAM1-3 up to Rubber's stack (rubber.h RB_STACK_TOP) */
#define RB_FAST_MAX   20000u          /* blocks up to this size go to SRAM1 while it lasts */

/* Rubber Band allocates everything when it is constructed (about 520 KB in 91 blocks, measured
 * on the bench) and nothing while it plays, so a bump allocator in two pools is enough, and
 * free() returns nothing (15 small frees at construction).
 *  - SRAM1, the free part after bank 2's .bss: the per-hop work arrays (8 and 16 KB each, swept
 *    every hop). Much faster than SDRAM on a cache miss, and nothing else uses it.
 *  - SDRAM, the top megabyte the build keeps from the sample allocator: the large ring buffers,
 *    read in order, and anything that does not fit in SRAM1. */
extern char __b2_bss_end[];
static uint32_t fast_at, slow_at = RB_HEAP_START;
uint32_t rb_fast_used, rb_slow_used;  /* for the bench */

typedef struct { uint32_t size, pad; } Head;   /* 8 bytes before every block, for realloc */

static void *take(size_t n)
{
    uint32_t need = ((uint32_t)n + sizeof(Head) + 15u) & ~15u;
    uint32_t *at;
    if (!fast_at)
        fast_at = ((uint32_t)__b2_bss_end + 15u) & ~15u;
    if (n <= RB_FAST_MAX && fast_at + need <= RB_FAST_END) {
        at = &fast_at;
        rb_fast_used += need;
    } else if (slow_at + need <= RB_HEAP_END) {
        at = &slow_at;
        rb_slow_used += need;
    } else {
        errno = ENOMEM;
        return 0;
    }
    Head *h = (Head *)(*at + 8u);   /* block itself 16-aligned */
    *at += need + 8u;
    h[-1].size = (uint32_t)n;
    return h;
}

void *malloc(size_t n) { return take(n); }
void free(void *p) { (void)p; }
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
    if (p)
        for (size_t i = 0; i < keep; i++)
            p[i] = ((char *)old)[i];
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

/* 64-bit atomics (Rubber Band's ratio fields): no other thread touches them. */
uint64_t __atomic_load_8(const volatile void *p, int order) { (void)order; return *(const volatile uint64_t *)p; }
void __atomic_store_8(volatile void *p, uint64_t v, int order) { (void)order; *(volatile uint64_t *)p = v; }
_Bool __atomic_is_lock_free(unsigned long size, const volatile void *p) { (void)p; return size <= 4; }
