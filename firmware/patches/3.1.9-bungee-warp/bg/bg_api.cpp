/* Bank-2 Bungee Basic C API (MPL-2.0 library; see build_bank2.sh).
 * Each grain is split over two blocks: specifyGrain + analyseGrain in one, synthesiseGrain
 * in the block that would otherwise run dry, so no block pays for a whole grain
 * (tools/lab/bungee/M7_RESULTS.md: peak 51% instead of 92%, same output). */
#include "bg_api.h"

#include "bungee/Bungee.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>

extern "C" {
extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start__[], __bss_end__[];
void bg_heap(void *base, uint32_t len);
}

#define PEND_MAX 4096  /* frames: a 2048-frame mix call plus a grain */
#ifndef BG_PRIME_HALVES
#define BG_PRIME_HALVES 1  /* half-grains per block while a launch primes (2: ~4 blocks at ~94%) */
#endif
#define PRIME_HALVES 8     /* preroll + output trailing by two grains: the 4th grain is the first heard */
#ifndef BG_XF
#define BG_XF 128          /* frames of raw -> Bungee crossfade at the handoff */
#endif
#ifndef BG_XF_POWER
#define BG_XF_POWER 0      /* 0: linear (raw and Bungee are in phase at the handoff), 1: equal-power */
#endif
#ifndef BG_PREROLL
#define BG_PREROLL 2       /* 2: preroll hop at speed 1, 1: at the clip's ratio, 0: none */
#endif

static Bungee::Stretcher<Bungee::Basic> *bg;
static Bungee::Request req;
static float *scratch;
static int scratch_frames;
static float *pend_l, *pend_r;
static int pend_n;
static bool analysed;  /* next grain analysed, not yet synthesised */
static bool primed;    /* first audible grain is pending: output can start */
static bool launch;    /* playing the raw clip while Bungee primes */
static bool pending;   /* bg_restart asked; set up on the next bg_mix (needs n) */
static float launch_ratio;
static float speed;    /* the clip's ratio, applied from the handoff on */
static bool unity_hop; /* the preroll grain runs at speed 1 (BG_PREROLL 2) */
static double start;   /* clip frame of the handoff: Bungee's first output frame */
static int raw_pos;    /* raw clip frames played since the launch */
static float xf_l[BG_XF], xf_r[BG_XF];
static bg_read_fn rd;
static void *rd_ctx;

void *operator new(size_t n) { return malloc(n ? n : 1); }
void *operator new[](size_t n) { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

/* -fno-exceptions: std::vector's length checks would otherwise pull in the EH runtime. */
namespace std {
void __throw_length_error(const char *) { for (;;) ; }
void __throw_bad_alloc() { for (;;) ; }
void __throw_bad_array_new_length() { for (;;) ; }
}

static void analyse()
{
    Bungee::InputChunk c = bg->specifyGrain(req);
    int k = c.end - c.begin;
    if (k > scratch_frames)
        k = scratch_frames;
    rd(rd_ctx, c.begin, k, scratch, scratch + scratch_frames);
    bg->analyseGrain(scratch, scratch_frames);
    analysed = true;
}

static void synthesise()
{
    Bungee::OutputChunk o;
    bg->synthesiseGrain(o);
    if (unity_hop) {
        /* Preroll hop done at speed 1: Bungee's phases now match the input at the
         * handoff, so the raw clip and Bungee line up there and crossfade cleanly.
         * Prerolling at the clip's ratio leaves them (512 - input hop) frames apart. */
        unity_hop = false;
        req.speed = 1.;
        bg->next(req);
        req.speed = speed;
    } else {
        req.speed = speed;
        bg->next(req);
    }
    analysed = false;
    double p0 = o.request[Bungee::OutputChunk::begin]->position;
    if (std::isnan(p0) || p0 < start)
        return;  /* preroll grain */
    int n = o.frameCount;
    if (n > PEND_MAX - pend_n)
        n = PEND_MAX - pend_n;
    for (int i = 0; i < n; i++) {
        pend_l[pend_n + i] = o.data[i];
        pend_r[pend_n + i] = o.data[o.channelStride + i];
    }
    pend_n += n;
}

static void half_step(int n)
{
    if (!analysed)
        analyse();
    else if (pend_n < n)
        synthesise();
}

extern "C" __attribute__((section(".bank2"))) int bg_init(void *arena, uint32_t arena_len)
{
    for (uint32_t *s = __data_load, *d = __data_start; d < __data_end;)
        *d++ = *s++;
    for (uint32_t *d = __bss_start__; d < __bss_end__;)
        *d++ = 0;
    if (!arena || arena_len < (1u << 20))
        return -1;
    bg_heap(arena, arena_len);
    bg = new Bungee::Stretcher<Bungee::Basic>({48000, 48000}, 2);
    scratch_frames = bg ? bg->maxInputFrameCount() : 0;
    scratch = (float *)malloc(2u * scratch_frames * sizeof(float));
    pend_l = (float *)malloc(2u * PEND_MAX * sizeof(float));
    pend_r = pend_l ? pend_l + PEND_MAX : 0;
    if (!bg || !scratch || !pend_l) {
        bg = 0;
        return -1;
    }
    bg_restart(1.f);
    return 0;
}

extern "C" __attribute__((section(".bank2"))) void bg_restart(float ratio)
{
    launch_ratio = ratio > 1e-3f ? ratio : 1.f;
    pending = true;
}

/* A launch plays the clip unstretched from frame 0 while Bungee primes, starting from the
 * frame where that raw playback will be when priming is done (start = priming time in
 * output frames). Both reach clip frame `start` at output frame `start`, so the handoff
 * is in time; from there Bungee runs at the stretched rate. */
static void begin_launch(int n)
{
    start = (double)((PRIME_HALVES + BG_PRIME_HALVES - 1) / BG_PRIME_HALVES) * n;
    req = {};
    req.position = start;
    req.speed = BG_PREROLL == 2 ? 1. : launch_ratio;
    speed = launch_ratio;
    unity_hop = BG_PREROLL == 2;
    req.pitch = 1.;
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
#if BG_PREROLL
    bg->preroll(req);
#endif
    pend_n = 0;
    analysed = false;
    primed = false;
    launch = true;
    raw_pos = 0;
    pending = false;
}

static float fade_in(int i)
{
    float x = (i + 0.5f) / BG_XF;
#if BG_XF_POWER
    return sinf(x * 1.5707963f);
#else
    return x;
#endif
}

static float fade_out(int i)
{
#if BG_XF_POWER
    return cosf((i + 0.5f) / BG_XF * 1.5707963f);
#else
    return 1.f - fade_in(i);
#endif
}

extern "C" __attribute__((section(".bank2"))) void bg_mix(bg_read_fn read, void *ctx, float *out_l,
                                                          float *out_r, int n, float ratio)
{
    int i;
    if (n <= 0)
        return;
    if (!bg || !read) {
        for (i = 0; i < n; i++)
            out_l[i] = out_r[i] = 0.f;
        return;
    }
    if (n > PEND_MAX / 2)
        n = PEND_MAX / 2;
    rd = read;
    rd_ctx = ctx;
    if (pending)
        begin_launch(n);
    speed = ratio > 1e-3f ? ratio : 1.f;
    if (!unity_hop)
        req.speed = speed;
    if (!primed) {
        /* Output chunks trail their grain by two grains (OutputChunk::begin is
         * grains[2]) and preroll adds one, so Bungee's first frame comes out of the
         * fourth grain: eight halves, one per block (~43 ms at 256 frames) instead of
         * a ~9M-cycle block. The raw clip plays meanwhile; the block that finishes
         * priming plays raw too, or it would also owe the next grain's analysis. */
        for (i = 0; i < BG_PRIME_HALVES && pend_n < n; i++)
            half_step(n);
        primed = pend_n >= n;
        read(ctx, raw_pos, n, out_l, out_r);
        raw_pos += n;
        return;
    }
    while (pend_n < n) {  /* only if the mix call grew past a grain */
        if (!analysed)
            analyse();
        synthesise();
    }
    for (i = 0; i < n; i++) {
        out_l[i] = pend_l[i];
        out_r[i] = pend_r[i];
    }
    if (launch) {
        /* Handoff: the raw clip carries on under Bungee's first BG_XF frames. */
        int k = n < BG_XF ? n : BG_XF;
        read(ctx, raw_pos, k, xf_l, xf_r);
        for (i = 0; i < k; i++) {
            float a = fade_in(i), b = fade_out(i);
            out_l[i] = a * out_l[i] + b * xf_l[i];
            out_r[i] = a * out_r[i] + b * xf_r[i];
        }
        launch = false;
    }
    pend_n -= n;
    for (i = 0; i < pend_n; i++) {
        pend_l[i] = pend_l[n + i];
        pend_r[i] = pend_r[n + i];
    }
    half_step(n);
}
