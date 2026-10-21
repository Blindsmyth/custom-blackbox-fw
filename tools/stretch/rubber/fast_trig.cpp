/* Approximate atan2 + sin LUT for USE_APPROXIMATE_ATAN2 / USE_FAST_PHASOR. */
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace RubberBand {

#ifdef USE_APPROXIMATE_ATAN2
float approximate_atan2f(float real, float imag)
{
    static const float pi = (float)M_PI;
    static const float pi2 = (float)M_PI / 2.f;
    float atan;
    if (real == 0.f) {
        if (imag > 0.0f) atan = pi2;
        else if (imag == 0.0f) atan = 0.0f;
        else atan = -pi2;
    } else {
        float z = imag / real;
        if (fabsf(z) < 1.f) {
            atan = z / (1.f + 0.28f * z * z);
            if (real < 0.f) {
                if (imag < 0.f) atan -= pi;
                else atan += pi;
            }
        } else {
            atan = pi2 - z / (z * z + 0.28f);
            if (imag < 0.f) atan -= pi;
        }
    }
    return atan;
}
#endif

#ifdef USE_FAST_PHASOR
/* C++ const at namespace scope is internal linkage unless extern. */
extern const float rb_fast_sin_lut[257] = {
#include "rubberband/src/common/fast_sin_lut.inc"
};
#endif

} /* namespace RubberBand */
