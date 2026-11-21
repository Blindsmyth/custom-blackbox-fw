/* CMP (Squish type): float port of Airwindows PurestSquish (MIT, Copyright (c) airwindows),
 * reference/airwindows/PurestSquish/PurestSquishProc.cpp processDoubleReplacing.
 *
 * Five vari-mu detector stages per channel on a high-passed sense signal: A/B/C take turns
 * (one per sample), D/E alternate with fpFlip, and the four resulting coefficients are averaged
 * into the gain. Dropped: the denormal guard noise and the (already disabled) dither.
 *
 * Kept on purpose: in the original the D/E branch squares stage E's coefficient in both arms
 * (lines 795 and 830 read muCoefficientEL/ER while the D arm updates stage D). That is how the
 * shipped plugin sounds, so the port does the same.
 */
#include "mc_dsp.h"
#include <math.h>
#include <string.h>

enum { SA, SB, SC, SD, SE };

void mc_cmp_reset(McCmp *s)
{
    memset(s, 0, sizeof *s);
    for (int k = 0; k < 5; k++)
        for (int ch = 0; ch < 2; ch++) { s->muSpeed[k][ch] = 10000.0f; s->muCoef[k][ch] = 1.0f; }
    for (int k = 0; k < 4; k++) s->lastCoef[k][0] = s->lastCoef[k][1] = 1.0f;
    s->merged[0] = s->merged[1] = 1.0f;
    s->count = 1;
    s->fpFlip = 1;
}

/* one detector stage: updates its iir, mu coefficient and speed; hp is high-passed in place */
static void stage(float *speed, float *coef, float *iir, float *hp, float threshold, float iirAmount,
                  float release, float fastest)
{
    *iir = (*iir * (1.0f - iirAmount)) + (*hp * iirAmount);
    *hp -= *iir;
    float sp = *speed, mc = *coef, a = fabsf(*hp);
    if (a > threshold) {
        float vary = threshold / a;
        float attack = sqrtf(fabsf(sp));
        mc = mc * (attack - 1.0f);
        mc += vary < threshold ? threshold : vary;
        mc = mc / attack;
    } else {
        mc = mc * ((sp * sp) - 1.0f);
        mc = mc + 1.0f;
        mc = mc / (sp * sp);
    }
    float ns = sp * (sp - 1.0f);
    ns = ns + fabsf(*hp * release) + fastest;
    *speed = ns / sp;
    *coef = mc;
}

void mc_cmp_process(McCmp *s, float sr, const McParams *p, float *L, float *R, int n, float *gr_db)
{
    const float os = sr / 44100.0f;
    const float A = p->sq_squish, B = p->sq_bassbloom, output = p->sq_output, wet = p->sq_mix;
    float t = 1.0f - (A * 0.5f);
    float threshold = 1.01f - (1.0f - t * t * t * t);
    float iirAmount = (B * B * B * B) / os;

    float muMakeup = 1.0f / threshold;
    float outMakeup = sqrtf(muMakeup);
    muMakeup = (muMakeup + outMakeup) * 0.5f;
    outMakeup *= 0.5f;

    float min_gain = 1.0f;
    float *io[2] = { L, R };
    for (int i = 0; i < n; i++) {
        if (s->count < 1 || s->count > 3) s->count = 1;
        int abc = s->count - 1, de = s->fpFlip ? SD : SE;
        for (int ch = 0; ch < 2; ch++) {
            float x = io[ch][i], dry = x;
            float merged = s->merged[ch];
            float release = merged * 32768.0f / os;
            float fastest = sqrtf(release);
            float lastCorrection = merged;

            if (muMakeup != 1.0f) x *= muMakeup;
            float hp = x, hpB = x;

            stage(&s->muSpeed[abc][ch], &s->muCoef[abc][ch], &s->iir[abc][ch], &hp, threshold,
                  iirAmount, release, fastest);
            float mc = s->muCoef[abc][ch];
            float lcA = mc * mc;
            merged = s->lastCoef[1][ch] + lcA;
            lcA = lcA * (1.0f - lastCorrection) + mc * lastCorrection;
            s->lastCoef[1][ch] = lcA;

            stage(&s->muSpeed[de][ch], &s->muCoef[de][ch], &s->iir[de][ch], &hpB, threshold,
                  iirAmount, release, fastest);
            float mcE = s->muCoef[SE][ch];                    /* see the note at the top */
            float lcC = mcE * mcE;
            merged += s->lastCoef[3][ch] + lcC;
            lcC = lcC * (1.0f - lastCorrection) + s->muCoef[de][ch] * lastCorrection;
            s->lastCoef[3][ch] = lcC;

            merged *= 0.25f;
            s->merged[ch] = merged;
            x *= merged;
            if (merged < min_gain) min_gain = merged;
            if (outMakeup != 1.0f) x *= outMakeup;
            if (output < 1.0f) x *= output;
            if (wet < 1.0f) x = (x * wet) + (dry * (1.0f - wet));
            io[ch][i] = x;
        }
        s->count++;
        s->fpFlip = !s->fpFlip;
    }
    if (gr_db) *gr_db = min_gain < 1.0f ? 20.0f * log10f(min_gain + 1e-9f) : 0.0f;
}
