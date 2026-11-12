// ref_render clr|cmp in.wav out.wav [p0 p1 ...]
// Renders a file through the unmodified Airwindows Mastering2 (clr) or PurestSquish (cmp) in
// double precision. Parameters are the plugins' raw 0..1 values in order; missing ones keep the
// plugin defaults (Mastering2's Dither is forced to 0 = none, the float port has no dither).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/wavio.h"

extern "C" void ref_m2_run(const float *, float, const float *, const float *, float *, float *, long);
extern "C" void ref_ps_run(const float *, float, const float *, const float *, float *, float *, long);

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s clr|cmp in.wav out.wav [p0 p1 ...]\n", argv[0]); return 2; }
    bool clr = !strcmp(argv[1], "clr");
    float p[7] = { 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f };   // Mastering2 defaults, no dither
    if (!clr) { p[0] = 0.0f; p[1] = 0.0f; p[2] = 1.0f; p[3] = 1.0f; } // PurestSquish defaults
    int np = clr ? 6 : 4;
    for (int i = 4; i < argc && i - 4 < np; i++) p[i - 4] = (float)atof(argv[i]);
    float *L, *R;
    int sr;
    long n = wav_read(argv[2], &L, &R, &sr);
    if (n < 0) return 1;
    std::vector<float> oL(n), oR(n);
    (clr ? ref_m2_run : ref_ps_run)(p, (float)sr, L, R, oL.data(), oR.data(), n);
    return wav_write_f32(argv[3], oL.data(), oR.data(), n, sr) ? 1 : 0;
}
