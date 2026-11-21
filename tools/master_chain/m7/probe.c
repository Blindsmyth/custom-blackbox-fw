/* Cortex-M7 cost probe for the master chain (run by measure.py under Unicorn).
 * stage: 0 whole chain (defaults), 1 CLR alone, 2 CMP Squish alone, 3 MAX alone,
 *        4 whole chain with CMP = Stock, 5 chain off (stock comp + stock limiter). */
#include "mc_dsp.h"
#include <string.h>

#define N 256
static McChain chain;
static McClr clr;
static McCmp cmp;
static McMax max;
static McParams prm;
static const float *inL, *inR;
static long frames, pos;
static int stage;
static float bufL[N], bufR[N];

__attribute__((used)) void probe_boot(void) {}

__attribute__((used)) void probe_setup(int s, const float *l, const float *r, long n)
{
    stage = s; inL = l; inR = r; frames = n; pos = 0;
    mc_default_params(&prm);
    if (s == 4) prm.cmp_type = 1.f;
    if (s == 5) mc_stock_params(&prm);
    mc_init(&chain, 48000.f);
    mc_set_params(&chain, &prm);
    mc_set_bpm(&chain, 120.f);
    mc_clr_reset(&clr);
    mc_cmp_reset(&cmp);
    mc_max_reset(&max, 48000.f);
}

__attribute__((used)) int probe_block(void)
{
    if (pos + N > frames) pos = 0;
    memcpy(bufL, inL + pos, sizeof bufL);
    memcpy(bufR, inR + pos, sizeof bufR);
    pos += N;
    switch (stage) {
    case 1: mc_clr_process(&clr, 48000.f, &prm, bufL, bufR, N, 0); break;
    case 2: mc_cmp_process(&cmp, 48000.f, &prm, bufL, bufR, N, 0); break;
    case 3: mc_max_process(&max, 48000.f, 120.f, &prm, bufL, bufR, N, 0, 0); break;
    default: mc_process(&chain, bufL, bufR, N); break;
    }
    return (int)(bufL[0] * 1000.f);
}

/* state sizes in bytes: 0 McClr, 1 McCmp, 2 McMax */
__attribute__((used)) unsigned probe_sizes(int which)
{
    return which == 0 ? (unsigned)sizeof(McClr) : which == 1 ? (unsigned)sizeof(McCmp) : (unsigned)sizeof(McMax);
}
