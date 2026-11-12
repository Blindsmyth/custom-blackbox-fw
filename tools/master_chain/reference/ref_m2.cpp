// Double-precision reference: the unmodified Airwindows Mastering2 class, run offline.
#include "Mastering2.h"
#include <vector>

extern "C" void ref_m2_run(const float *params, float sr, const float *inL, const float *inR,
                           float *outL, float *outR, long n)
{
    Mastering2 fx(nullptr);
    fx.sampleRate = sr;
    for (int i = 0; i < kNumParameters; i++) fx.setParameter(i, params[i]);
    std::vector<double> l(512), r(512), ol(512), orr(512);
    for (long pos = 0; pos < n; pos += 512) {
        int k = (int)(n - pos < 512 ? n - pos : 512);
        for (int i = 0; i < k; i++) { l[i] = inL[pos + i]; r[i] = inR[pos + i]; }
        double *in[2] = { l.data(), r.data() }, *out[2] = { ol.data(), orr.data() };
        fx.processDoubleReplacing(in, out, k);
        for (int i = 0; i < k; i++) { outL[pos + i] = (float)ol[i]; outR[pos + i] = (float)orr[i]; }
    }
}
