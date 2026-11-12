/* CLR: float port of Airwindows Mastering2 (MIT, Copyright (c) Chris Johnson / airwindows),
 * reference/airwindows/Mastering2/Mastering2Proc.cpp processDoubleReplacing.
 *
 * Same stages, same order, same constants: drive in, Sidepass (side low-pass fed through sin and
 * subtracted), Air3 treble split, Kalman mid/bass and bass/sub splits, Zoom shapers per band,
 * drive out, ClipOnly2, Sinew (Glue). Dropped: the denormal guard noise and every dither mode.
 * Air3 and the Kalman bands are written once per channel; the original runs L then R on the same
 * kind of state, so the order does not change the result.
 */
#include "mc_dsp.h"
#include <math.h>
#include <string.h>

#define HALF_PI 1.57079633f

/* Every sin/cos here sees an argument within +-pi/2 in practice (Zoom clamps it, Sidepass and
 * Sinew run on small, clipped signals). Taylor to x^9 / x^10 is good to ~4e-6 / 5e-7 there and
 * costs a handful of multiplies instead of a libm call on the M7; outside it falls back to libm. */
static inline float fsin(float x)
{
    if (x > HALF_PI || x < -HALF_PI) return sinf(x);
    float x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6.f + x2 * (1.f / 120.f + x2 * (-1.f / 5040.f + x2 * (1.f / 362880.f)))));
}

static inline float fcos(float x)
{
    if (x > HALF_PI || x < -HALF_PI) return cosf(x);
    float x2 = x * x;
    return 1.f + x2 * (-0.5f + x2 * (1.f / 24.f + x2 * (-1.f / 720.f + x2 * (1.f / 40320.f + x2 * (-1.f / 3628800.f)))));
}

/* air[] per channel, 16 entries (Mastering2.h enum, minus the L/R suffix) */
enum { pvA1, pvS1, accS1, acc2S1, pvA2, pvS2, accS2, acc2S2, pvA3, pvS3, accS3, pvA4, pvS4,
       gndavg, outA, gainA, AIR_N };
/* kalM[] / kalS[] per channel, 12 entries */
enum { prevSamp1, prevSlew1, accSlew1, prevSamp2, prevSlew2, accSlew2, prevSamp3, prevSlew3,
       accSlew3, kalGain, kalOut, kalAvg, KAL_N };

void mc_clr_reset(McClr *s)
{
    memset(s, 0, sizeof *s);
    s->fpFlip = 1;
}

static void air3(float *a, float dry, float cap, float split, float *mid, float *treble)
{
    a[pvS4] = a[pvA4] - a[pvA3]; a[pvS3] = a[pvA3] - a[pvA2];
    a[pvS2] = a[pvA2] - a[pvA1]; a[pvS1] = a[pvA1] - dry;
    a[accS3] = a[pvS4] - a[pvS3]; a[accS2] = a[pvS3] - a[pvS2];
    a[accS1] = a[pvS2] - a[pvS1];
    a[acc2S2] = a[accS3] - a[accS2]; a[acc2S1] = a[accS2] - a[accS1];
    a[outA] = -(a[pvA1] + a[pvS3] + a[acc2S2] - ((a[acc2S2] + a[acc2S1]) * 0.5f));
    a[gainA] *= 0.5f; a[gainA] += fabsf(dry - a[outA]) * 0.5f;
    if (a[gainA] > cap) a[gainA] = cap;
    a[pvA4] = a[pvA3]; a[pvA3] = a[pvA2];
    a[pvA2] = a[pvA1]; a[pvA1] = (a[gainA] * a[outA]) + dry;
    float m = dry - ((a[outA] * 0.5f) + (dry * split));
    float t = (m + a[gndavg]) * 0.5f; a[gndavg] = m; m = t;
    *mid = m;
    *treble = dry - m;
}

/* returns the low band; the caller subtracts it from its input */
static float kalman(float *k, float in, float kal)
{
    k[prevSlew3] += k[prevSamp3] - k[prevSamp2]; k[prevSlew3] *= 0.5f;
    k[prevSlew2] += k[prevSamp2] - k[prevSamp1]; k[prevSlew2] *= 0.5f;
    k[prevSlew1] += k[prevSamp1] - in; k[prevSlew1] *= 0.5f;
    k[accSlew2] += k[prevSlew3] - k[prevSlew2]; k[accSlew2] *= 0.5f;
    k[accSlew1] += k[prevSlew2] - k[prevSlew1]; k[accSlew1] *= 0.5f;
    k[accSlew3] += (k[accSlew2] - k[accSlew1]); k[accSlew3] *= 0.5f;
    k[kalOut] += k[prevSamp1] + k[prevSlew2] + k[accSlew3]; k[kalOut] *= 0.5f;
    k[kalGain] += fabsf(in - k[kalOut]) * kal * 8.0f; k[kalGain] *= 0.5f;
    if (k[kalGain] > kal * 0.5f) k[kalGain] = kal * 0.5f;
    k[kalOut] += (in * (1.0f - (0.68f + (kal * 0.157f))));
    k[prevSamp3] = k[prevSamp2]; k[prevSamp2] = k[prevSamp1];
    k[prevSamp1] = (k[kalGain] * k[kalOut]) + ((1.0f - k[kalGain]) * in);
    float low = (k[kalOut] + k[kalAvg]) * 0.5f;
    k[kalAvg] = k[kalOut];
    return low;
}

/* Zoom: > 0 is Density (louder body), < 0 boosts the hottest peaks and cuts softer material */
static float zoom(float x, float z)
{
    if (z > 0.0f) {
        float closer = x * HALF_PI;
        if (closer > HALF_PI) closer = HALF_PI;
        if (closer < -HALF_PI) closer = -HALF_PI;
        x = (x * (1.0f - z)) + (fsin(closer) * z);
    } else if (z < 0.0f) {
        float farther = fabsf(x) * HALF_PI;
        farther = farther > HALF_PI ? 1.0f : 1.0f - fcos(farther);
        if (x > 0.0f) x = (x * (1.0f + z)) - (farther * z * HALF_PI);
        else if (x < 0.0f) x = (x * (1.0f + z)) + (farther * z * HALF_PI);
    }
    return x;
}

static float clip_only2(McClr *s, int ch, float x, int spacing, int *hit)
{
    float *last = &s->lastSample[ch], *inter = s->intermediate[ch];
    if (x > 4.0f) x = 4.0f;
    if (x < -4.0f) x = -4.0f;
    if (s->wasPos[ch]) {
        if (x < *last) *last = 0.7058208f + (x * 0.2609148f);
        else *last = 0.2491717f + (*last * 0.7390851f);
    }
    s->wasPos[ch] = 0;
    if (x > 0.9549925859f) { s->wasPos[ch] = 1; x = 0.7058208f + (*last * 0.2609148f); *hit = 1; }
    if (s->wasNeg[ch]) {
        if (x > *last) *last = -0.7058208f + (x * 0.2609148f);
        else *last = -0.2491717f + (*last * 0.7390851f);
    }
    s->wasNeg[ch] = 0;
    if (x < -0.9549925859f) { s->wasNeg[ch] = 1; x = -0.7058208f + (*last * 0.2609148f); *hit = 1; }
    inter[spacing] = x;
    x = *last;
    for (int k = spacing; k > 0; k--) inter[k - 1] = inter[k];
    *last = inter[0];
    return x;
}

void mc_clr_process(McClr *s, float sr, const McParams *p, float *L, float *R, int n, float *clipped)
{
    const float A = p->clr_sidepass, B = p->clr_glue, C = p->clr_scope;
    const float D = p->clr_skronk, E = p->clr_girth, F = p->clr_drive;
    const float os = sr / 44100.0f;

    float iirSide = A * A * A * (0.1f / os);
    float threshSinew = (0.25f + ((1.0f - B) * 0.333f)) / os;
    float depthSinew = 1.0f - (1.0f - B) * (1.0f - B);

    float trebleZoom = C - 0.5f;
    float trebleGain = (trebleZoom * fabsf(trebleZoom)) + 1.0f;
    if (trebleGain > 1.0f) trebleGain = powf(trebleGain, 3.0f + sqrtf(os));

    float midZoom = D - 0.5f;
    float midGain = (midZoom * fabsf(midZoom)) + 1.0f;
    float kalMid = 0.35f - (D * 0.25f);
    float kalSub = 0.45f + (D * 0.25f);

    float bassZoom = (E * 0.5f) - 0.25f;
    float bassGain = (-bassZoom * fabsf(bassZoom)) + 1.0f;
    float subGain = ((E * 0.25f) - 0.125f) + 1.0f;
    if (subGain < 1.0f) subGain = 1.0f;

    float driveIn = (F - 0.5f) + 1.0f;
    float driveOut = (-(F - 0.5f) * fabsf(F - 0.5f)) + 1.0f;

    int spacing = (int)floorf(os);
    if (spacing < 1) spacing = 1;
    if (spacing > 16) spacing = 16;
    float cap = 0.3f * sqrtf(os), split = 0.457f - (0.017f * os);

    int hit = 0;
    for (int i = 0; i < n; i++) {
        float inL = L[i] * driveIn, inR = R[i] * driveIn;

        float mid = inL + inR, side = inL - inR, temp = side;
        if (s->fpFlip) { s->iirA = (s->iirA * (1.0f - iirSide)) + (temp * iirSide); temp = s->iirA; }
        else { s->iirB = (s->iirB * (1.0f - iirSide)) + (temp * iirSide); temp = s->iirB; }
        s->iirC = (s->iirC * (1.0f - iirSide)) + (temp * iirSide); temp = s->iirC;
        side -= fsin(temp);
        s->fpFlip = !s->fpFlip;
        float x[2] = { (mid + side) * 0.5f, (mid - side) * 0.5f };

        for (int ch = 0; ch < 2; ch++) {
            float dry = x[ch], midX, trebleX;
            air3(s->air + ch * AIR_N, dry, cap, split, &midX, &trebleX);
            float bassX = kalman(s->kalM + ch * KAL_N, midX, kalMid);
            midX -= bassX;
            float subX = kalman(s->kalS + ch * KAL_N, bassX, kalSub);
            bassX -= subX;

            float out = subX * subGain;
            out += zoom(bassX, bassZoom) * bassGain;
            out += zoom(midX, midZoom) * midGain;
            out += zoom(trebleX, trebleZoom) * trebleGain;
            out *= driveOut;

            out = clip_only2(s, ch, out, spacing, &hit);

            float last = s->lastSinew[ch], t = out;
            float sinew = threshSinew * fcos(last * last);
            if (out - last > sinew) t = last + sinew;
            if (-(out - last) > sinew) t = last - sinew;
            s->lastSinew[ch] = t;
            x[ch] = (out * (1.0f - depthSinew)) + (t * depthSinew);
        }
        L[i] = x[0];
        R[i] = x[1];
    }
    if (clipped) *clipped = hit ? 1.0f : 0.0f;
}
