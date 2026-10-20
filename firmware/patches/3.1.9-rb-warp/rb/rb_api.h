#ifndef RB_API_H
#define RB_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bank-2 leaf API. Mixxx “faster” R2 only. One stereo Clip voice.
 * Working memory lives in an SDRAM arena allocated once via stock bump 0x0804436C. */

int rb_init(float sample_rate);
void rb_reset(void);
/* time_ratio: clip playback rate (>1 = faster / shorter). n = frames this mix call. */
void rb_mix(const float *in_l, const float *in_r, float *out_l, float *out_r,
            int n, float time_ratio);

#ifdef __cplusplus
}
#endif

#endif
