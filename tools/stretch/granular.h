/* A model of the Blackbox 3.1.9 Granular pad engine (docs/map-319.md "Granular pad engine"):
 * one voice, 8 grain slots of which N (3..8, from Density) play, Hann windows, no level
 * normalisation, the stock scheduler, RNG and period lock. Grains play at rate 1 (pitch 0, note
 * = root, 48 kHz source), so the pitch never changes; the playhead scans the file at Speed.
 *
 * Synced variant (gran_sync): the playhead scans at the tempo ratio instead of Speed, locked to
 * the output clock, so the grain cloud follows the beat the way Clip Stretch does. Size, Density,
 * Scatter, Window and Pan Rnd stay as they are. */
#ifndef GRANULAR_H
#define GRANULAR_H

#include <stdint.h>

#define GRAN_SLOTS 8

typedef int (*GranReadFn)(void *sample, float *dest, int count, int pos, int chan);

typedef struct {
    int active;
    int64_t pos;       /* source frame of the next sample */
    int delay;         /* output frames before it starts */
    int remain;        /* frames left */
    int win;           /* window position */
    int size;
    float gl, gr;      /* pan gains */
} GranGrain;

typedef struct {
    /* Gran tab, stock ranges */
    int size_v;        /* Grain Size 0..1000 */
    int density_v;     /* Density 0..1000 */
    int scatter_v;     /* Scatter 0..1000 */
    int window_v;      /* Window 0..1000 */
    int speed_v;       /* Speed 0..2000 (1000 = 1x) */
    int panrnd_v;      /* Pan Rnd 0..1000 */
    int root;          /* Root Note 0 (none) .. 127: the period lock */
    int sync;          /* 1: the playhead follows the tempo ratio, locked to the clock */
    /* engine */
    int size, n, gap, scatter, window;
    float speed, panrnd;
    int64_t now, base;
    double play;       /* playhead, source frames (unwrapped) */
    int64_t play_at;   /* the time (now) play is at: the block's start */
    int64_t ref_play, ref_time;
    uint32_t seed;
    GranGrain g[GRAN_SLOTS];
} GranVoice;

#ifdef GRAN_NO_TABLES
extern float *gran_src_buf;   /* the device build: set to 2 x GRAN_SRC floats before gran_process */
#endif

void gran_init(GranVoice *v);
void gran_set(GranVoice *v, int size, int density, int scatter, int window, int speed, int panrnd,
              int root, int sync);
/* n frames into out_l/out_r (added, as stock sums into the voice buffer; clear them first).
 * ratio: tempo / clip tempo, used when sync is on. reset: a new note-on. */
void gran_process(GranVoice *v, void *sample, int length, int stereo, GranReadFn read, float *out_l,
                  float *out_r, int n, float ratio, int reset);

#endif
