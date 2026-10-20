/* Bank-2 Rubber Band C API. Links against RubberBandOpt / CMSIS (see build_bank2.sh).
 * GPL-2.0-or-later when linked with Rubber Band. */
#include "rb_api.h"

#include "rubberband/RubberBandStretcher.h"
#include "src/common/FFT.h"

#include <cstddef>
#include <cstdint>
#include <new>

using RubberBand::RubberBandStretcher;

typedef void *(*bump_fn)(uint32_t bytes);
#define fw_bump ((bump_fn)(0x0804436Cu | 1u))

static uint8_t *arena;
static size_t arena_len;
static size_t arena_used;
static RubberBandStretcher *rb;
static float sr_hz = 48000.f;
static float feed_l[4096], feed_r[4096];

extern "C" void *rb_arena_alloc(size_t n)
{
    n = (n + 15u) & ~15u;
    if (!arena || arena_used + n > arena_len)
        return 0;
    void *p = arena + arena_used;
    arena_used += n;
    return p;
}

void *operator new(size_t n)
{
    void *p = rb_arena_alloc(n);
    return p ? p : (void *)arena; /* never throw; NO_EXCEPTIONS build */
}
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
void operator delete[](void *, size_t) noexcept {}

static void ensure_arena(void)
{
    if (arena)
        return;
    const uint32_t want = 8u * 1024u * 1024u;
    arena = (uint8_t *)fw_bump(want);
    arena_len = arena ? want : 0;
    arena_used = 0;
}

extern "C" __attribute__((section(".bank2"))) int rb_init(float sample_rate)
{
    sr_hz = sample_rate > 1.f ? sample_rate : 48000.f;
    ensure_arena();
    if (!arena)
        return -1;
    RubberBand::FFT::setDefaultImplementation("cmsis");
    return 0;
}

extern "C" __attribute__((section(".bank2"))) void rb_reset(void)
{
    rb = 0;
    arena_used = 0;
}

extern "C" __attribute__((section(".bank2"))) void rb_mix(
    const float *in_l, const float *in_r, float *out_l, float *out_r,
    int n, float time_ratio)
{
    int i;
    if (n <= 0)
        return;
    if (!arena && rb_init(sr_hz) != 0) {
        for (i = 0; i < n; i++) {
            out_l[i] = in_l ? in_l[i] : 0.f;
            out_r[i] = in_r ? in_r[i] : 0.f;
        }
        return;
    }
    float tr = time_ratio > 1e-3f ? time_ratio : 1.f;
    if (!rb) {
        RubberBandStretcher::Options o =
            RubberBandStretcher::OptionProcessRealTime |
            RubberBandStretcher::OptionThreadingNever;
        void *mem = rb_arena_alloc(sizeof(RubberBandStretcher));
        if (!mem) {
            for (i = 0; i < n; i++) {
                out_l[i] = in_l ? in_l[i] : 0.f;
                out_r[i] = in_r ? in_r[i] : 0.f;
            }
            return;
        }
        rb = new (mem) RubberBandStretcher((size_t)sr_hz, 2, o, 1.0 / (double)tr, 1.0);
        size_t pad = rb->getPreferredStartPad();
        static float z[4096];
        const float *zp[2] = { z, z };
        while (pad > 0) {
            size_t k = pad < 4096 ? pad : 4096;
            rb->process(zp, k, false);
            pad -= k;
        }
    } else {
        rb->setTimeRatio(1.0 / (double)tr);
    }

    /* Feed this mix block, retrieve n frames. */
    size_t fed = 0;
    while ((int)rb->available() < n) {
        size_t need = rb->getSamplesRequired();
        if (need == 0)
            need = 256;
        if (need > 4096)
            need = 4096;
        for (size_t k = 0; k < need; k++) {
            size_t idx = fed + k;
            if (idx >= (size_t)n)
                idx = (size_t)n - 1;
            feed_l[k] = in_l ? in_l[idx] : 0.f;
            feed_r[k] = in_r ? in_r[idx] : 0.f;
        }
        const float *ptrs[2] = { feed_l, feed_r };
        rb->process(ptrs, need, false);
        fed += need;
        if (fed > (size_t)n * 8u) /* safety */
            break;
    }
    float *oo[2] = { out_l, out_r };
    rb->retrieve(oo, (size_t)n);
}
