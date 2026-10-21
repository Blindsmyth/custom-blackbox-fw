// Minimal newlib syscalls for the bare-metal cost probe: a heap and nothing else.
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

extern char __heap_start[], __heap_end[];
static char *brk = __heap_start;

void *_sbrk(ptrdiff_t n)
{
    char *p = brk;
    if (brk + n > __heap_end) {
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
void _exit(int c) { (void)c; for (;;) __asm__ volatile("bkpt #0"); }
size_t probe_heap_used(void) { return (size_t)(brk - __heap_start); }

/* Single-threaded stand-ins for what the C++ runtime expects from an OS. */
#include <stdint.h>
#include <sys/time.h>
void *__dso_handle = 0;
void _init(void) {}
void _fini(void) {}
int _gettimeofday(struct timeval *tv, void *tz) { (void)tz; if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; } return 0; }
uint64_t __atomic_load_8(const volatile void *p, int order) { (void)order; return *(const volatile uint64_t *)p; }
void __atomic_store_8(volatile void *p, uint64_t v, int order) { (void)order; *(volatile uint64_t *)p = v; }
_Bool __atomic_is_lock_free(unsigned long size, const volatile void *p) { (void)p; return size <= 4; }
