// Cortex-M7 cost probe for Rubber Band (tools/lab/rb/m7/measure.py runs it in Unicorn).
// Same options as the lab / Mixxx; one stereo stream, 256-frame blocks (the Blackbox
// audio period). The harness writes the clip at CLIP and calls these entry points.
#include "rubberband/RubberBandStretcher.h"
#ifdef HAVE_CMSIS_DSP
#include "src/common/FFT.h"
#endif

#include <cstdint>
#include <cstdlib>

using RubberBand::RubberBandStretcher;

extern "C" void __libc_init_array(void);

static RubberBandStretcher *rb;
static const float *clip_l, *clip_r;
static int clip_len;
static size_t read_pos;
static float feed_l[4096], feed_r[4096];
static float out_l[256], out_r[256];

static void feed(size_t n)
{
    const float *p[2] = { feed_l, feed_r };
    while (n > 0) {
        size_t k = n < 4096 ? n : 4096;
        for (size_t i = 0; i < k; i++) {
            feed_l[i] = clip_l[read_pos];
            feed_r[i] = clip_r[read_pos];
            if (++read_pos >= (size_t)clip_len)
                read_pos = 0;
        }
        rb->process(p, k, false);
        n -= k;
    }
}

extern "C" {

__attribute__((used)) void probe_boot(void) { __libc_init_array(); }

// mode 0 faster (R2), 1 finer (R3), 2 finer + short window
__attribute__((used)) int probe_setup(int mode, float ratio, const float *l, const float *r, int len)
{
    RubberBandStretcher::Options o = RubberBandStretcher::OptionProcessRealTime |
                                     RubberBandStretcher::OptionThreadingNever;
    if (mode >= 1)
        o |= RubberBandStretcher::OptionEngineFiner | RubberBandStretcher::OptionChannelsTogether;
    if (mode == 2)
        o |= RubberBandStretcher::OptionWindowShort;
#ifdef HAVE_CMSIS_DSP
    RubberBand::FFT::setDefaultImplementation("cmsis");
#endif
    clip_l = l;
    clip_r = r;
    clip_len = len;
    read_pos = 0;
    rb = new RubberBandStretcher(48000, 2, o, 1.0 / ratio, 1.0);
    size_t pad = rb->getPreferredStartPad();
    static float z[4096];
    const float *zp[2] = { z, z };
    while (pad > 0) {
        size_t k = pad < 4096 ? pad : 4096;
        rb->process(zp, k, false);
        pad -= k;
    }
    return (int)rb->getStartDelay();
}

// One 256-frame output block.
__attribute__((used)) int probe_block(void)
{
    float *o[2] = { out_l, out_r };
    int done = 0;
    while (done < 256) {
        while (rb->available() < 1)
            feed(rb->getSamplesRequired() ? rb->getSamplesRequired() : 256);
        int k = rb->available();
        if (k > 256 - done)
            k = 256 - done;
        float *oo[2] = { out_l + done, out_r + done };
        rb->retrieve(oo, k);
        done += k;
    }
    (void)o;
    return done;
}

__attribute__((used)) float *probe_out(void) { return out_l; }

}
