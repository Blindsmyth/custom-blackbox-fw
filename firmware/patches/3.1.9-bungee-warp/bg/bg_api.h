#ifndef BG_API_H
#define BG_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bank-2 leaf API: Bungee Basic (MPL-2.0), default 512-frame synthesis hop, one stereo
 * Clip voice. Bank-2 .data/.bss live in the shared SRAM4 voice slot past its header
 * (bank2_only.ld), so they are lost whenever another Warp mode takes the slot: bank 1
 * calls bg_init again each time it (re)claims the slot. The working set (~0.5 MB) lives
 * in an SDRAM arena that bank 1 allocates once per boot and passes in. */

/* Fills l/r with frames [pos, pos + n) of the clip; pos may be negative (fill zeros)
 * or past the end (wrap: the clip loops). */
typedef void (*bg_read_fn)(void *ctx, int pos, int n, float *l, float *r);

/* Runs the C runtime init (copy .data, zero .bss), puts the heap on the arena and
 * builds the stretcher. 0 = ok, -1 = arena missing or too small. */
int bg_init(void *arena, uint32_t arena_len);
/* Start again from clip frame 0 (new launch). Output is silent until primed. */
void bg_restart(float ratio);
/* n frames of output; ratio > 1 = faster tempo. Does at most half a grain of work. */
void bg_mix(bg_read_fn read, void *ctx, float *out_l, float *out_r, int n, float ratio);

#ifdef __cplusplus
}
#endif

#endif
