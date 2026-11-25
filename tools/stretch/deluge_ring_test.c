/* Native check of deluge.c's device window (DELUGE_PERC_RING) against the Warp Lab's whole-file
 * deluge_perc, on the same audio: every byte the window holds, after every call, at several tempo
 * ratios, through the file's wrap and after jumps of the play position (a cue, a relaunch).
 * Also: the pixels the hop search reads are held once the window has caught up.
 *
 *   cc -O2 -DDELUGE_PERC_RING=256 -DDELUGE_SHARED_SCRATCH -DDELUGE_CACHE=32 -DDELUGE_FAST_SEARCH -DDELUGE_RESYNC=8192 \
 *      -DDELUGE_RESYNC_1X=256 -I tools/stretch \
 *      tools/stretch/deluge_ring_test.c tools/stretch/deluge.c -o deluge_ring_test && ./deluge_ring_test
 *
 * Prints one line per case and "max_diff N exact_from_start E missing M" at the end (run_tests.py
 * t_lineup_deluge_perc reads it). Exit status 0 when every check passed. */
#include "deluge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { const float *l, *r; int length; long reads; } Src;

static int src_read(void *ctx, float *dest, int count, int pos, int chan)
{
    Src *s = (Src *)ctx;
    const float *x = chan ? s->r : s->l;
    int i;
    for (i = 0; i < count; i++)
        dest[i] = x[pos + i];
    s->reads += count;
    return count;
}

static float scratch[DELUGE_SCRATCH_ALL];

/* A drum loop over a pad: a kick-ish burst every unit, hats on the off 8ths, a soft chord under it. */
static void make(float *l, float *r, int n, double spu, unsigned seed)
{
    int i;
    for (i = 0; i < n; i++) {
        double u = fmod(i, spu), h = fmod(i + spu / 2, spu);
        double kick = exp(-u / 900.) * sin(2 * M_PI * (60. + 90. * exp(-u / 300.)) * u / 48000.);
        double hat, pad;
        seed = seed * 1103515245u + 12345u;
        hat = exp(-h / 150.) * ((double)((seed >> 8) & 0xFFFF) / 32768. - 1.) * 0.3;
        pad = 0.08 * (sin(2 * M_PI * 220. * i / 48000.) + sin(2 * M_PI * 277.2 * i / 48000.));
        l[i] = (float)(0.7 * kick + hat + pad);
        r[i] = (float)(0.7 * kick - 0.5 * hat + pad);
    }
}

static int max_diff = 0, exact_start = 1, missing = 0, fails = 0;

/* Compare every held pixel with the reference; bytes of a window started at the file's start
 * (and run on from there) must be exact. */
static int check(const DelugeVoice *v, const uint8_t *ref, int n_pix, int exact)
{
    const DelugePerc *q = &v->perc;
    int u, worst = 0;
    for (u = q->lo; u < q->next; u++) {
        int d = abs((int)q->ring[u & (DELUGE_PERC_RING - 1)] - (int)ref[u % n_pix]);
        if (d > worst)
            worst = d;
    }
    if (exact && worst)
        exact_start = 0;
    if (worst > max_diff)
        max_diff = worst;
    return worst;
}

/* Pixels the next hop's search can read: [pos - 17, pos + 95) at most. Counted once caught up. */
static int coverage(const DelugeVoice *v, int n_pix)
{
    int hp = v->host_i >> DELUGE_PIX_SHIFT, p, miss = 0;
    for (p = hp - 17; p < hp + 95; p++)
        if (deluge_perc_at(v, p, n_pix) < 0)
            miss++;
    return miss;
}

static void run_case(const char *name, int length, float ratio, int jumps)
{
    static float l[400000], r[400000], out_l[256], out_r[256];
    static uint8_t ref[400000 / DELUGE_PIX + 2];
    static DelugeVoice v;
    Src s;
    int n_pix = (length >> DELUGE_PIX_SHIFT) + 1, call, worst = 0, miss = 0, calls, past_start = 0;
    unsigned seed = 77;
    make(l, r, length, 48000. * 60. / 130. / 4., 1234);
    s.l = l;
    s.r = r;
    s.length = length;
    s.reads = 0;
    deluge_perc(&s, length, 1, src_read, ref, n_pix);
    v.scratch = scratch;
    v.magic = 0;
    calls = (int)(3.5 * length / ratio / 256.);       /* through the file's wrap, more than 3 times */
    for (call = 0; call < calls; call++) {
        int since_jump = call, w;
        if (jumps && call > 0 && call % 97 == 0) {
            seed = seed * 1103515245u + 12345u;
            v.host_i = (int)((seed >> 4) % (unsigned)length);  /* as lanes.cpp host_sync */
            v.host_f = 0.f;
        }
        if (jumps)
            since_jump = call % 97;
        deluge_process(&v, &s, length, 1, src_read, 0, 0, out_l, out_r, 256, ratio, call == 0);
        w = check(&v, ref, n_pix, !jumps);
        if (w > worst)
            worst = w;
        if (v.host_i >= 17 * DELUGE_PIX)
            past_start = 1;
        if (since_jump >= 24 && (jumps || past_start))
            miss += coverage(&v, n_pix);
    }
    missing += miss;
    if (worst > 2 || miss)
        fails++;
    /* The window keeps moving on, lap after lap (a short Clip Deck file changes as it plays). */
    if (!jumps && v.perc.next < (int)(calls * 256 * ratio / DELUGE_PIX)) {
        printf("  window stopped at pixel %d\n", v.perc.next);
        fails++;
    }
    printf("%-34s len %6d  %.2fx  calls %5d  max diff %d  missing %d  hops %d\n", name, length, ratio, calls, worst,
           miss, v.n_hops);
}

int main(void)
{
    run_case("drums 0.69x", 199999, 90.f / 130.f, 0);
    run_case("drums 1.3x", 199999, 1.3f, 0);
    run_case("drums 0.25x, length % 128 == 0", 128 * 1500, 0.25f, 0);
    run_case("drums 4x", 199999, 4.f, 0);
    run_case("short file (20 pixels) 0.69x", 2500, 90.f / 130.f, 0);
    run_case("jumps 0.69x", 199999, 90.f / 130.f, 1);
    run_case("jumps 1.3x", 199999, 1.3f, 1);
    printf("max_diff %d exact_from_start %d missing %d\n", max_diff, exact_start, missing);
    if (!exact_start)
        fails++;
    return fails ? 1 : 0;
}
