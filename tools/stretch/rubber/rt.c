/* Bank 2 runtime for Rubber Band: newlib's malloc over the top megabyte of SDRAM, and the few
 * OS calls newlib and libstdc++ expect. Single-threaded: only the audio task runs bank 2 code
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

static char *brk = (char *)RB_HEAP_START;

void *_sbrk(ptrdiff_t n)
{
    char *p = brk;
    if (n < 0 || (uint32_t)(brk + n) > RB_HEAP_END) {
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
void _exit(int c) { (void)c; for (;;) __asm__ volatile("wfi"); }
int _gettimeofday(struct timeval *tv, void *tz) { (void)tz; if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; } return 0; }

void *__dso_handle = 0;
void _init(void) {}
void _fini(void) {}

/* 64-bit atomics (Rubber Band's ratio fields): no other thread touches them. */
uint64_t __atomic_load_8(const volatile void *p, int order) { (void)order; return *(const volatile uint64_t *)p; }
void __atomic_store_8(volatile void *p, uint64_t v, int order) { (void)order; *(volatile uint64_t *)p = v; }
_Bool __atomic_is_lock_free(unsigned long size, const volatile void *p) { (void)p; return size <= 4; }
