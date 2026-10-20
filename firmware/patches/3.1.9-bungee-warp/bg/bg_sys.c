/* Bare-metal syscalls for the bank-2 Bungee leaf. newlib malloc/free run on _sbrk over the
 * SDRAM arena bank 1 passes to bg_init (real free, unlike RB's no-op delete: Eigen resizes). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

static uint8_t *heap, *heap_end, *brk;

void bg_heap(void *base, uint32_t len)
{
    heap = brk = (uint8_t *)(((uintptr_t)base + 7u) & ~(uintptr_t)7u);
    heap_end = (uint8_t *)base + len;
}

void *_sbrk(ptrdiff_t n)
{
    uint8_t *p = brk;
    if (!heap || n < 0 || brk + n > heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += n;
    return p;
}

int _write(int f, const void *b, int n) { (void)f; (void)b; return n; }
int _read(int f, void *b, int n) { (void)f; (void)b; (void)n; return 0; }
int _close(int f) { (void)f; return -1; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return 0; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; errno = EINVAL; return -1; }
void _exit(int c) { (void)c; for (;;) ; }
void abort(void) { for (;;) ; }
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

void *__dso_handle = 0;
void _init(void) {}
void _fini(void) {}

/* Empty unwind tables for -fno-exceptions */
__attribute__((section(".ARM.exidx"), used))
static const uint32_t __exidx_empty[1];
extern const uint32_t __exidx_start __attribute__((alias("__exidx_empty")));
extern const uint32_t __exidx_end __attribute__((alias("__exidx_empty")));
