/* MAX: one-knob look-ahead maximizer.
 *
 * Maximize 0..1 drives the input 0..+12 dB into a fixed -0.3 dBFS ceiling. The required gain of
 * every incoming sample goes through a sliding-window minimum (monotonic queue, O(1) per sample)
 * and a box filter, both as long as the look-ahead delay, so the gain has fully arrived when that
 * sample leaves the delay: no overshoot by construction (a safety clamp counts any miss in
 * over_db). Rising gain follows a one-pole release of an eighth note at the current tempo
 * (x0.5 / x1 / x2), 250 ms without one.
 */
#include "mc_dsp.h"
#include <math.h>
#include <string.h>

#define DQ (MC_MAX_LOOKAHEAD + 2)

void mc_max_reset(McMax *s, float sr)
{
    memset(s, 0, sizeof *s);
    int len = (int)(0.0015f * sr + 0.5f);
    if (len < 1) len = 1;
    if (len > MC_MAX_LOOKAHEAD) len = MC_MAX_LOOKAHEAD;
    s->len = len;
    for (int i = 0; i < MC_MAX_LOOKAHEAD; i++) s->box[i] = 1.0f;
    s->box_sum = (float)len;
    s->gain = 1.0f;
}

static float release_ms(float bpm, float relx)
{
    float ms = 250.0f;
    if (bpm >= 20.0f && bpm <= 400.0f) {
        ms = 30000.0f / bpm;                     /* an eighth note */
        if (ms < 80.0f) ms = 80.0f;
        if (ms > 600.0f) ms = 600.0f;
    }
    int k = (int)(relx + 0.5f);
    return ms * (k <= 0 ? 0.5f : (k >= 2 ? 2.0f : 1.0f));
}

void mc_max_process(McMax *s, float sr, float bpm, const McParams *p, float *L, float *R, int n,
                    float *gr_db, float *over_db)
{
    const int len = s->len;
    const float ceil = MC_MAX_CEILING;
    float amt = p->max_amount < 0.0f ? 0.0f : (p->max_amount > 1.0f ? 1.0f : p->max_amount);
    const float drive = powf(10.0f, 12.0f * amt * 0.05f);
    const float relc = 1.0f - expf(-1.0f / (sr * 0.001f * release_ms(bpm, p->max_relx)));

    float min_gain = 1.0f, over = 0.0f;
    for (int i = 0; i < n; i++) {
        float xl = L[i] * drive, xr = R[i] * drive;
        float pk = fmaxf(fabsf(xl), fabsf(xr));
        float req = pk > ceil ? ceil / pk : 1.0f;

        /* minimum of the required gain over the last len+1 samples */
        while (s->dq_tail != s->dq_head && s->dq_val[(s->dq_tail + DQ - 1) % DQ] >= req)
            s->dq_tail = (s->dq_tail + DQ - 1) % DQ;
        s->dq_val[s->dq_tail] = req;
        s->dq_t[s->dq_tail] = s->t;
        s->dq_tail = (s->dq_tail + 1) % DQ;
        while ((unsigned short)(s->t - s->dq_t[s->dq_head]) > (unsigned short)len) s->dq_head = (s->dq_head + 1) % DQ;
        float wmin = s->dq_val[s->dq_head];
        s->t++;

        s->box_sum += wmin - s->box[s->box_pos];
        s->box[s->box_pos] = wmin;
        if (++s->box_pos >= len) {
            s->box_pos = 0;
            float sum = 0.0f;                    /* re-add exactly once per lap: no drift */
            for (int k = 0; k < len; k++) sum += s->box[k];
            s->box_sum = sum;
        }
        float target = s->box_sum / (float)len;

        if (target < s->gain) s->gain = target;
        else s->gain += (target - s->gain) * relc;
        if (s->gain < min_gain) min_gain = s->gain;

        float ol = s->delay[0][s->pos] * s->gain, or_ = s->delay[1][s->pos] * s->gain;
        s->delay[0][s->pos] = xl;
        s->delay[1][s->pos] = xr;
        if (++s->pos >= len) s->pos = 0;

        float m = fmaxf(fabsf(ol), fabsf(or_));
        if (m > ceil) {
            float g = ceil / m;
            ol *= g; or_ *= g;
            float o = -20.0f * log10f(g);
            if (o > over) over = o;
        }
        L[i] = ol;
        R[i] = or_;
    }
    if (gr_db) *gr_db = 20.0f * log10f(min_gain + 1e-9f);
    if (over_db) *over_db = over;
}
