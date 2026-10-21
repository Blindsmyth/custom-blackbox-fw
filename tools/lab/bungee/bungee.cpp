// Warp Lab candidate: Bungee Basic (MPL-2.0), a phase vocoder that also plays
// in reverse. Same export shape as rb/rb.cpp. The clip loops, so each grain's
// input chunk is read modulo the clip length and Request::position runs on
// unbounded (forwards or backwards).
// Build: tools/lab/bungee/build.sh (Emscripten).
#include "bungee/Bungee.h"

#include <cmath>
#include <cstdlib>

static float *in_l, *in_r;
static int length, capacity;
static float out_l[4096], out_r[4096];
static float *scratch;  // planar grain input, channelStride = scratch_frames
static int scratch_frames;
static float pend_l[8192], pend_r[8192];  // synthesised frames not yet handed out
static int pend_n;
static Bungee::Stretcher<Bungee::Basic> *bg;
static Bungee::Request req;
static double start;
static int dir = 1;
static int mode = -1;
static int hop_adjust;

extern "C" {

// Clip buffers; call with the frame count, then write into both.
__attribute__((export_name("bg_alloc"))) int bg_alloc(int frames)
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

__attribute__((export_name("bg_in_l"))) float *bg_in_l() { return in_l; }
__attribute__((export_name("bg_in_r"))) float *bg_in_r() { return in_r; }
__attribute__((export_name("bg_out_l"))) float *bg_out_l() { return out_l; }
__attribute__((export_name("bg_out_r"))) float *bg_out_r() { return out_r; }

static int wrap(int p)
{
    p %= length;
    return p < 0 ? p + length : p;
}

// One grain: feed its input chunk, append its output to pend_*. Output from
// preroll grains (before the start position) is dropped so A/B stays aligned.
static void grain()
{
    Bungee::InputChunk c = bg->specifyGrain(req);
    int k = c.end - c.begin;
    if (k > scratch_frames)
        k = scratch_frames;
    for (int i = 0; i < k; i++) {
        int p = wrap(c.begin + i);
        scratch[i] = in_l[p];
        scratch[scratch_frames + i] = in_r[p];
    }
    bg->analyseGrain(scratch, scratch_frames);
    Bungee::OutputChunk o;
    bg->synthesiseGrain(o);
    bg->next(req);
    double p0 = o.request[Bungee::OutputChunk::begin]->position;
    if (std::isnan(p0) || (p0 - start) * dir < 0)
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

// which: bit 0 = reverse, bit 1 = 256-frame synthesis hop (log2SynthesisHopAdjust
// -1: one grain per device block instead of per two). ratio > 1 is a faster
// tempo. Renders n (<= 4096) samples into bg_out_*.
__attribute__((export_name("bg_render"))) int bg_render(int which, int n, float ratio, int reset)
{
    if (length <= 0 || n <= 0)
        return 0;
    if (n > 4096)
        n = 4096;
    if (!(ratio > 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;
    int adjust = which & 2 ? -1 : 0;
    if (!bg || adjust != hop_adjust) {
        delete bg;
        free(scratch);
        bg = new Bungee::Stretcher<Bungee::Basic>({48000, 48000}, 2, adjust);
        hop_adjust = adjust;
        scratch_frames = bg->maxInputFrameCount();
        scratch = (float *)calloc(2 * scratch_frames, sizeof(float));
        reset = 1;
    }
    if (reset || which != mode) {
        mode = which;
        dir = which & 1 ? -1 : 1;
        start = dir > 0 ? 0. : (double)length;
        req = {};
        req.position = start;
        req.speed = dir * ratio;
        req.pitch = 1.;
        req.reset = true;
        req.resampleMode = resampleMode_autoOut;
        bg->preroll(req);
        pend_n = 0;
    }
    req.speed = dir * ratio;
    while (pend_n < n)
        grain();
    for (int i = 0; i < n; i++) {
        out_l[i] = pend_l[i];
        out_r[i] = pend_r[i];
    }
    pend_n -= n;
    for (int i = 0; i < pend_n; i++) {
        pend_l[i] = pend_l[n + i];
        pend_r[i] = pend_r[n + i];
    }
    return n;
}

}
