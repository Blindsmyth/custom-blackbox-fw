#include "wavio.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

long wav_read(const char *path, float **L, float **R, int *sample_rate)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fprintf(stderr, "%s: not a WAV file\n", path); fclose(f); return -1;
    }
    int fmt = 0, ch = 0, bits = 0, sr = 0;
    long frames = -1;
    unsigned char ck[8];
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t size = u32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned char b[40] = { 0 };
            size_t want = size < sizeof b ? size : sizeof b;
            if (fread(b, 1, want, f) != want) break;
            if (size > want) fseek(f, (long)(size - want), SEEK_CUR);
            fmt = u16(b); ch = u16(b + 2); sr = (int)u32(b + 4); bits = u16(b + 14);
            if (fmt == 0xFFFE && size >= 26) fmt = u16(b + 24);     /* WAVE_FORMAT_EXTENSIBLE */
        } else if (!memcmp(ck, "data", 4)) {
            if (!ch || !bits) break;
            int bps = bits / 8;
            frames = (long)(size / (uint32_t)(bps * ch));
            unsigned char *raw = malloc((size_t)size);
            *L = malloc(sizeof(float) * (size_t)frames);
            *R = malloc(sizeof(float) * (size_t)frames);
            if (!raw || !*L || !*R || fread(raw, 1, size, f) != size) { frames = -1; free(raw); break; }
            for (long i = 0; i < frames; i++) {
                float v[2] = { 0, 0 };
                for (int c = 0; c < ch && c < 2; c++) {
                    const unsigned char *p = raw + ((size_t)i * ch + c) * bps;
                    if (fmt == 3 && bits == 32) { float x; memcpy(&x, p, 4); v[c] = x; }
                    else if (bits == 16) v[c] = (int16_t)u16(p) / 32768.f;
                    else if (bits == 24) v[c] = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | (uint32_t)p[2] << 24)) / 2147483648.f;
                    else if (bits == 32) v[c] = (int32_t)u32(p) / 2147483648.f;
                }
                (*L)[i] = v[0];
                (*R)[i] = ch == 1 ? v[0] : v[1];
            }
            free(raw);
            break;
        } else {
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    if (frames < 0) fprintf(stderr, "%s: unsupported or truncated WAV\n", path);
    if (sample_rate) *sample_rate = sr;
    return frames;
}

int wav_write_f32(const char *path, const float *L, const float *R, long frames, int sr)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    uint32_t data = (uint32_t)frames * 8, rate = (uint32_t)sr, byte_rate = rate * 8, riff = 36 + data, fmt = 16;
    uint16_t tag = 3, ch = 2, align = 8, bits = 32;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (long i = 0; i < frames; i++) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
    fclose(f);
    return 0;
}
