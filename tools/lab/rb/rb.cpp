// Warp Lab reference: Rubber Band Library (GPL-2.0-or-later) set up the way
// Mixxx does it (src/engine/bufferscalers/enginebufferscalerubberband.cpp):
//   "Rubberband (faster)": OptionProcessRealTime (R2 engine)
//   "Rubberband (better)": OptionProcessRealTime | OptionEngineFiner |
//                          OptionChannelsTogether [| OptionWindowShort]
// Not a Blackbox candidate (FFT phase vocoder); here for comparison only.
// Build: tools/lab/rb/build.sh (Emscripten, Rubber Band's single-file build).
#include "rubberband/RubberBandStretcher.h"

#include <cstdlib>
#include <cstring>

using RubberBand::RubberBandStretcher;

static float *in_l, *in_r;
static int length, capacity;
static float out_l[4096], out_r[4096];
static float feed_l[16384], feed_r[16384];
static RubberBandStretcher *rb;
static int engine = -1;
static size_t read_pos;
static float cur_ratio = 1.f;

extern "C" {

// Clip buffers; call with the frame count, then write into both.
__attribute__((export_name("rb_alloc"))) int rb_alloc(int frames)
{
    if (frames > capacity) {
        free(in_l);
        free(in_r);
        in_l = (float *)calloc(frames, sizeof(float));
        in_r = (float *)calloc(frames, sizeof(float));
        capacity = in_l && in_r ? frames : 0;
    }
    length = capacity ? frames : 0;
    return length;
}

__attribute__((export_name("rb_in_l"))) float *rb_in_l() { return in_l; }
__attribute__((export_name("rb_in_r"))) float *rb_in_r() { return in_r; }
__attribute__((export_name("rb_out_l"))) float *rb_out_l() { return out_l; }
__attribute__((export_name("rb_out_r"))) float *rb_out_r() { return out_r; }

static void feed(size_t n)
{
    const float *ptrs[2] = { feed_l, feed_r };
    while (n > 0) {
        size_t k = n < 16384 ? n : 16384;
        for (size_t i = 0; i < k; i++) {
            feed_l[i] = in_l[read_pos];
            feed_r[i] = in_r[read_pos];
            if (++read_pos >= (size_t)length)
                read_pos = 0;
        }
        rb->process(ptrs, k, false);
        n -= k;
    }
}

// engine: 0 = faster (R2), 1 = finer (R3), 2 = finer + short window.
// ratio > 1 is a faster tempo. Renders n (<= 4096) samples into rb_out_*.
__attribute__((export_name("rb_render"))) int rb_render(int which, int n, float ratio, int reset)
{
    if (length <= 0 || n <= 0)
        return 0;
    if (n > 4096)
        n = 4096;
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    if (!rb || which != engine) {
        RubberBandStretcher::Options o = RubberBandStretcher::OptionProcessRealTime |
                                         RubberBandStretcher::OptionThreadingNever;
        if (which >= 1)
            o |= RubberBandStretcher::OptionEngineFiner | RubberBandStretcher::OptionChannelsTogether;
        if (which == 2)
            o |= RubberBandStretcher::OptionWindowShort;
        delete rb;
        rb = new RubberBandStretcher(48000, 2, o, 1.0 / ratio, 1.0);
        engine = which;
        reset = 1;
    }
    if (reset) {
        rb->reset();
        rb->setTimeRatio(1.0 / ratio);
        read_pos = 0;
        // Prime as the library recommends: pad, then drop the start delay.
        size_t pad = rb->getPreferredStartPad();
        if (pad > 0) {
            static float zl[16384], zr[16384];
            const float *z[2] = { zl, zr };
            while (pad > 0) {
                size_t k = pad < 16384 ? pad : 16384;
                rb->process(z, k, false);
                pad -= k;
            }
        }
        size_t drop = rb->getStartDelay();
        while (drop > 0) {
            float *o[2] = { out_l, out_r };
            while ((size_t)rb->available() < 1)
                feed(rb->getSamplesRequired() ? rb->getSamplesRequired() : 256);
            size_t k = rb->available();
            if (k > drop)
                k = drop;
            if (k > 4096)
                k = 4096;
            rb->retrieve(o, k);
            drop -= k;
        }
        cur_ratio = ratio;
    } else if (ratio != cur_ratio) {
        rb->setTimeRatio(1.0 / ratio);
        cur_ratio = ratio;
    }
    float *o[2] = { out_l, out_r };
    int done = 0;
    while (done < n) {
        while (rb->available() < 1)
            feed(rb->getSamplesRequired() ? rb->getSamplesRequired() : 256);
        int k = rb->available();
        if (k > n - done)
            k = n - done;
        float *oo[2] = { out_l + done, out_r + done };
        rb->retrieve(oo, k);
        done += k;
    }
    (void)o;
    return n;
}

}
