/* Minimal WAV I/O for the offline tools: reads PCM 16/24/32-bit and float32, writes float32. */
#ifndef WAVIO_H
#define WAVIO_H
#ifdef __cplusplus
extern "C" {
#endif

/* Reads a stereo (or mono, duplicated) file. Returns frames, or -1. Caller frees *L and *R. */
long wav_read(const char *path, float **L, float **R, int *sample_rate);
int wav_write_f32(const char *path, const float *L, const float *R, long frames, int sample_rate);

#ifdef __cplusplus
}
#endif
#endif
