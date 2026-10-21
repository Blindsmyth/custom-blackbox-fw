/* Bank 2 runtime for Rubber Band: its allocator (SRAM1 only), and the few OS calls newlib and
 * libstdc++ expect. Single-threaded: only the audio task runs bank 2 code
 * after rubber_boot (which runs once, before audio uses it). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

#define RB_FAST_END   0x30045000u     /* SRAM1-3 up to Rubber's stack (rubber.h RB_STACK_*) */

/* Rubber Band allocates everything when it is constructed (about 245 KB with the Blackbox
 * patch's fixed window and hops, measured on the bench) and nothing while it plays, so a bump
 * allocator over the free part of SRAM1, after bank 2's .bss, is enough; free() returns nothing
 * (a few small frees at construction). Nothing goes to SDRAM: the samples keep all of it. A
 * block that does not fit sets rb_heap_failed and returns 0; rubber_boot then gives up
 * ("Rubber! mem"). */
extern char __b2_bss_end[];
static uint32_t fast_at;
uint32_t rb_fast_used;  /* for the bench */
int rb_heap_failed;

typedef struct { uint32_t size, pad; } Head;   /* 8 bytes before every block, for realloc */

static void *take(size_t n)
{
    uint32_t need = ((uint32_t)n + sizeof(Head) + 15u) & ~15u;
    Head *h;
    if (!fast_at)
        fast_at = ((uint32_t)__b2_bss_end + 15u) & ~15u;
    if (fast_at + need + 8u > RB_FAST_END) {
        rb_heap_failed = 1;
        errno = ENOMEM;
        return 0;
    }
    rb_fast_used += need + 8u;
    h = (Head *)(fast_at + 8u);   /* the block itself 16-aligned */
    fast_at += need + 8u;
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
