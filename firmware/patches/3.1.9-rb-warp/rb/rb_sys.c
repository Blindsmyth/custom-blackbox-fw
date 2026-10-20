/* Bare-metal syscalls for bank-2 RB leaf. Heap = SDRAM arena via rb_arena_alloc. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

extern void *rb_arena_alloc(size_t n);
extern int rb_init(float sample_rate);

void *_sbrk(ptrdiff_t n)
{
    if (n <= 0)
        return (void *)0;
    void *p = rb_arena_alloc((size_t)n);
    if (!p) {
        errno = ENOMEM;
        return (void *)-1;
    }
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

uint64_t __atomic_load_8(const volatile void *p, int order)
{
    (void)order;
    return *(const volatile uint64_t *)p;
}
void __atomic_store_8(volatile void *p, uint64_t v, int order)
{
    (void)order;
    *(volatile uint64_t *)p = v;
}
_Bool __atomic_is_lock_free(unsigned long size, const volatile void *p)
{
    (void)p;
    return size <= 4;
}

/* Empty unwind tables for -fno-exceptions / discarded personality */
__attribute__((section(".ARM.exidx"), used))
static const uint32_t __exidx_empty[1];
extern const uint32_t __exidx_start __attribute__((alias("__exidx_empty")));
extern const uint32_t __exidx_end __attribute__((alias("__exidx_empty")));
