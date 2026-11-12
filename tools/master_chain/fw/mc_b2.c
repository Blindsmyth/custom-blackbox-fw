/* Master chain, bank 2 side: RbExt.master (lanes.cpp). Runs on the audio task through
 * rb_call_on_stack (bank 1, mixer.c); the chain's state stays here in B2RAM (.b2bss, zeroed by
 * rubber_boot), the parameters come from SRAM4 (mc_fw.h). */
#include "mc_fw.h"

static McChain chain;
static unsigned booted;

void mc_b2_run(unsigned op, unsigned l, unsigned r, unsigned n)
{
    McShared *s = MC_SHARED;
    float *L = (float *)l, *R = (float *)r;
    if (s->magic != MC_SHARED_MAGIC)
        return;
    if (!booted || op == MC_OP_RESET) {
        mc_init(&chain, 48000.f);
        booted = 1;
        if (op == MC_OP_RESET)
            return;
    }
    if (!L || !R || (int)n <= 0)
        return;
    chain.p = s->p;
    chain.bpm = s->bpm;
    if (s->hold_req != s->hold_seen) {       /* BACK on CMP / MAX */
        s->cmp_hold = s->max_hold = 0.f;
        for (int b = 0; b < MC_BANDS; b++) s->eq_hold[b] = 0.f;
        s->hold_seen = s->hold_req;
    }
    switch (op) {
    case MC_OP_EQ:
        mc_run_eq(&chain, L, R, (int)n);
        for (int b = 0; b < MC_BANDS; b++) {
            s->m.eq_dyn_db[b] = chain.m.eq_dyn_db[b];
            s->m.eq_det_db[b] = chain.m.eq_det_db[b];
            float d = chain.m.eq_dyn_db[b], h = s->eq_hold[b];
            if ((d < 0 ? -d : d) > (h < 0 ? -h : h)) s->eq_hold[b] = d;
        }
        break;
    case MC_OP_PRE:
        mc_run_pre(&chain, L, R, (int)n);
        s->m.clr_clip = chain.m.clr_clip;
        s->m.cmp_gr_db = chain.m.cmp_gr_db;
        s->m.comp_in_db = chain.m.comp_in_db;
        if (chain.m.cmp_gr_db < s->cmp_hold) s->cmp_hold = chain.m.cmp_gr_db;
        break;
    case MC_OP_MAX:
        mc_run_max(&chain, L, R, (int)n);
        s->m.max_gr_db = chain.m.max_gr_db;
        s->m.max_over_db = chain.m.max_over_db;
        s->m.out_peak_db = chain.m.out_peak_db;
        if (chain.m.max_gr_db < s->max_hold) s->max_hold = chain.m.max_gr_db;
        break;
    case MC_OP_LIMIT:
        mc_run_limit(&chain, L, R, (int)n);
        s->m.lim_gr_db = chain.m.lim_gr_db;
        s->m.out_peak_db = chain.m.out_peak_db;
        if (chain.m.lim_gr_db < s->max_hold) s->max_hold = chain.m.lim_gr_db;
        break;
    default:
        break;
    }
}
