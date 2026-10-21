// Cortex-M7 cost probe for Bungee Basic (tools/lab/rb/m7/measure.py runs it in Unicorn).
// Same entry points as rb/m7/rbm7.cpp; same driving as ../bungee.cpp (looping clip,
// speed = ratio, preroll output dropped). One stereo stream, 256-frame blocks.
#include "bungee/Bungee.h"

#include <cmath>
#include <cstdlib>

extern "C" void __libc_init_array(void);

static const float *clip_l, *clip_r;
static int clip_len;
static float *scratch;
static int scratch_frames;
static float pend_l[8192], pend_r[8192];
static int pend_n;
static float out_l[256], out_r[256];
static Bungee::Stretcher<Bungee::Basic> *bg;
static Bungee::Request req;

#ifndef BG_HOP_ADJUST
#define BG_HOP_ADJUST 0  // -1 halves the synthesis hop (512 -> 256 frames at 48 kHz)
#endif

static int wrap(int p)
{
    p %= clip_len;
    return p < 0 ? p + clip_len : p;
}

#ifndef BG_SPLIT
#define BG_SPLIT 0  // 1: analyse and synthesise each grain in alternate blocks
#endif

static bool analysed;  // next grain analysed, not yet synthesised

static void analyse()
{
    Bungee::InputChunk c = bg->specifyGrain(req);
    int k = c.end - c.begin;
    if (k > scratch_frames)
        k = scratch_frames;
    for (int i = 0; i < k; i++) {
        int p = wrap(c.begin + i);
        scratch[i] = clip_l[p];
        scratch[scratch_frames + i] = clip_r[p];
    }
    bg->analyseGrain(scratch, scratch_frames);
    analysed = true;
}

static void synthesise()
{
    Bungee::OutputChunk o;
    bg->synthesiseGrain(o);
    bg->next(req);
    analysed = false;
    double p0 = o.request[Bungee::OutputChunk::begin]->position;
    if (std::isnan(p0) || p0 < 0)
        return;
    int n = o.frameCount;
    if (n > 8192 - pend_n)
        n = 8192 - pend_n;
    for (int i = 0; i < n; i++) {
        pend_l[pend_n + i] = o.data[i];
        pend_r[pend_n + i] = o.data[o.channelStride + i];
    }
    pend_n += n;
}

static void grain()
{
    if (!analysed)
        analyse();
    synthesise();
}

extern "C" {

__attribute__((used)) void probe_boot(void) { __libc_init_array(); }

// mode is ignored (Basic only); ratio > 1 is a faster tempo.
__attribute__((used)) int probe_setup(int mode, float ratio, const float *l, const float *r, int len)
{
    (void)mode;
    clip_l = l;
    clip_r = r;
    clip_len = len;
    bg = new Bungee::Stretcher<Bungee::Basic>({48000, 48000}, 2, BG_HOP_ADJUST);
    scratch_frames = bg->maxInputFrameCount();
    scratch = (float *)calloc(2 * scratch_frames, sizeof(float));
    req = {};
    req.position = 0.;
    req.speed = ratio;
    req.pitch = 1.;
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
    bg->preroll(req);
    pend_n = 0;
    analysed = false;
    return scratch_frames;
}

__attribute__((used)) int probe_block(void)
{
    while (pend_n < 256)
        grain();
    for (int i = 0; i < 256; i++) {
        out_l[i] = pend_l[i];
        out_r[i] = pend_r[i];
    }
    pend_n -= 256;
    for (int i = 0; i < pend_n; i++) {
        pend_l[i] = pend_l[256 + i];
        pend_r[i] = pend_r[256 + i];
    }
#if BG_SPLIT
    // Work ahead by half a grain so each block pays for at most one half:
    // analyse the next grain while frames are still pending, synthesise it in
    // the block that runs dry.
    if (!analysed)
        analyse();
    else if (pend_n < 256)
        synthesise();
#endif
    return 256;
}

__attribute__((used)) float *probe_out(void) { return out_l; }

}
