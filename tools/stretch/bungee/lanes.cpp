// Warp Fill / Chop / Grain, and Stutter on slices (3.1.T): the Warp Lab's segment player and
// synced Granular, rendering a Clip's lanes in flash bank 2.
//
// repitch.S hook_lane calls rubber_lanes (through b2pair.c rb_lanes) in the audio task for every
// lane of a Stutter, Fill, Chop or Grain Clip, in place of stock's lane render (Stutter only with
// Sync = Slice: on a grid it declines, and stock's grains play). Unlike Bungee there is no
// single voice: each lane has its own engine (LANE_SLOTS at once), so any number of pads can use
// these modes, a relaunch can release on one lane while the new note starts on the other, and a
// choke group behaves as stock. All of it is cheap enough to run in the audio block itself.
//
//   Fill   tools/stretch/beats.c, Loop tail: segments start on the Sync grid (or at the slices
//          with Sync = Slice) and play at 1x; at a slower tempo the gap until the next segment is
//          filled by looping the segment's tail (loop length matched on the waveform).
//   Chop   the same with Gate: the segment fades out where its audio ends, so a slower tempo
//          leaves a gap, a faster one cuts it (a sliced-loop player retriggering on the grid).
//   Stutter with Sync = Slice: stock Stretch's run-on (a segment plays on into the next one's
//          audio until that is due), with the segments at the slices. Stock gates the gap there.
//   Grain  tools/stretch/granular.c, the stock Granular engine's model, synced: the playhead
//          scans at the tempo ratio, locked to the clock. Fixed settings, the Warp Lab's for pads
//          and melodic material: Size 200, Density 1000, Scatter 9, Window 100, Pan Rnd 300.
//          With Sync on, the playhead lands on each segment's start as the clock reaches it.
//
// Segment timing follows the clock while the transport runs (the launch's tick, the tempo map,
// the Clip's length in ticks, as Bungee's clock lock), and the tempo ratio when it is stopped.
// Around the engines, the stock lane's voice: Level and pan, envelope, pad filter, pad feedback
// and end of voice (lane.h).
#include "rubber.h"
#include "lane.h"

extern "C" {
#include "beats.h"
#include "granular.h"
int rubber_lanes(unsigned clip, unsigned lane, unsigned mix, unsigned ctx);
void lanes_boot(void);
}

#include <cstddef>
#include <cstdint>

#define CLIP_FLAG   0xBB1u      /* repitch.S: the Clip's Warp value */
#define WARP_STUTTER 0
#define WARP_FILL   1
#define WARP_CHOP   3
#define WARP_GRAIN  4
#define LANE_SLOTS  8           /* lanes rendering at once; a ninth plays stock grains */
#define STARTS_MAX  2048        /* segment starts per Clip (longer grids are thinned) */
#define MAX_N       2048        /* frames per call at most */
#define SUB         256         /* engine calls in blocks of this */
#define SYNC_SLICE  0xF0000     /* clip+0x220 (Sync size in ticks) for Sync = Slice */

struct Slot {
    unsigned lane, clip;        /* lane 0: free */
    int mode;
    int64_t stamp;              /* lane+0xE0 of the note this slot plays */
    void *sample;
    int length, stereo;
    double anchor;              /* clock tick at the note's first frame */
    bool anchored;
    double played;              /* source frames since the launch (unwrapped), the segment clock */
    bool pending;               /* the note's reset is not passed to the engine yet (launch pre-delay) */
    int seg;                    /* Grain: the segment the playhead last landed on */
    LaneGain gain;
#ifdef WARP_DECK_SEQ
    DeckSeq seq;                /* Clip Deck's unit sequence (lane.h), on a note-value Sync grid */
#endif
    union {
        BeatsVoice b;
        GranVoice g;
    } e;
};

// The leaf's SDRAM pool (b2pair.c rb_pair allocates RbExt.pool_bytes at boot).
struct Pool {
    uint32_t magic;
    float scratch[BEATS_SCRATCH];   /* Fill's loop match, shared: used inside one call only */
    float gran_src[2 * SUB];        /* Grain's reads (granular.c gran_src_buf), shared likewise */
    float mono_r[SUB];              /* the right channel of a mono buffer, dropped */
    int32_t starts[STARTS_MAX];     /* segment starts of the Clip `key_clip` */
    int n_starts;
    unsigned key_clip;
    uint32_t key[4];
#ifdef WARP_DECK_SEQ
    struct Virt {               /* Fill / Chop under Clip Deck: the virtual file the player reads */
        void *sample;
        int length;
        unsigned clip;
        const DeckSeq *q;
        int32_t starts[SEQ_RING];
        int vlen;
    } virt;
#endif
    Slot slot[LANE_SLOTS];
};
#define POOL_MAGIC 0x4C4E4153u      /* "SANL" */

struct RbExtInit {
    rb_lanes_fn lanes;
    uint32_t pool_bytes;
};
extern "C" __attribute__((used)) const RbExtInit rb_ext = { rubber_lanes, sizeof(Pool) };

static Pool *pool()
{
    const RbVoice *v = (const RbVoice *)RB_VOICE;
    if (!v->pool || v->pool_bytes < sizeof(Pool))
        return nullptr;
    return (Pool *)v->pool;
}

// rubber_boot: the pool comes uninitialised from stock's allocator.
void lanes_boot(void)
{
    Pool *p = pool();
    if (!p)
        return;
    uint8_t *w = (uint8_t *)p;
    for (size_t i = 0; i < sizeof(Pool); i++)
        w[i] = 0;
    p->magic = POOL_MAGIC;
}

// Sync = Slice with slices: the segments are the slice table (clip+0x24C, count +0x250).
static bool sync_slices(unsigned clip)
{
    return L8(clip, 0x1F5) && L32(clip, 0x220) == SYNC_SLICE && L32(clip, 0x224) == 0 && L32(clip, 0x250) > 0;
}

// The Clip renders here: Fill, Chop, Grain, and Stutter on slices.
static bool lane_mode(unsigned clip, int m)
{
    return m == WARP_FILL || m == WARP_CHOP || m == WARP_GRAIN || (m == WARP_STUTTER && sync_slices(clip));
}

// This lane's slot, or a free one: never used, its lane off, or its Clip no longer on these modes.
static Slot *find_slot(Pool *p, unsigned lane)
{
    for (int i = 0; i < LANE_SLOTS; i++)
        if (p->slot[i].lane == lane)
            return &p->slot[i];
    for (int i = 0; i < LANE_SLOTS; i++) {
        Slot *s = &p->slot[i];
        if (!s->lane || !lane_on(s->lane) || !lane_mode(s->clip, L8(s->clip, CLIP_FLAG))) {
            s->lane = 0;
            return s;
        }
    }
    return nullptr;
}

// The Clip's segments as Sync cuts them (FUN_08064A94 / FUN_08065550): Sync = Slice, the slice
// table (clip+0x24C, count +0x250); Sync on, unit k at k x frames-per-unit (clip+0x1FC, a float)
// for the units of clip+0x210; Sync None, the whole file. Kept until the Clip or its grid changes.
static const int32_t *segments(Pool *p, unsigned clip, int length, int *count)
{
    bool sync = L8(clip, 0x1F5) != 0;
    bool slices = sync_slices(clip);
    uint32_t key[4] = { (uint32_t)sync | (uint32_t)slices << 1, (uint32_t)L32(clip, 0x1FC),
                        (uint32_t)L32(clip, 0x210), slices ? (uint32_t)L32(clip, 0x250) : (uint32_t)length };
    if (p->key_clip == clip && p->key[0] == key[0] && p->key[1] == key[1] && p->key[2] == key[2] &&
        p->key[3] == key[3]) {
        *count = p->n_starts;
        return p->starts;
    }
    int n = 0;
    p->starts[n++] = 0;
    if (slices) {
        const uint32_t *t = *(const uint32_t *const *)(clip + 0x24C);
        int c = L32(clip, 0x250);
        for (int i = 0; t && i < c && n < STARTS_MAX; i++)
            if ((int)t[i] > p->starts[n - 1] && (int)t[i] < length)
                p->starts[n++] = (int32_t)t[i];
    } else if (sync) {
        float spu = LF(clip, 0x1FC);
        int units = L32(clip, 0x210);
        int every = units / STARTS_MAX + 1;    /* a very long grid: every m-th unit */
        for (int k = every; k < units && n < STARTS_MAX && spu > 0.f; k += every) {
            int at = (int)((float)k * spu);
            if (at > p->starts[n - 1] && at < length)
                p->starts[n++] = at;
        }
    }
    p->n_starts = n;
    p->key_clip = clip;
    for (int i = 0; i < 4; i++)
        p->key[i] = key[i];
    *count = n;
    return p->starts;
}

static int seg_of(const int32_t *starts, int n, double pos)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if ((double)starts[mid] <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

#ifdef WARP_DECK_SEQ
// Under Clip Deck, Fill and Chop play a virtual file of SEQ_RING units: virtual unit j is the sequence
// index i with i % SEQ_RING == j, whatever unit deck.c chose for it. The player's segments, pre-roll
// and tail loop then land on the right audio after a loop wrap or a cue jump, with the timeline still
// running straight on. Reads map back to the file here.
#define SEQ_LEAD (256 + 64)     /* frames ahead the next unit is decided: Beats' pre-roll and a margin */

static int virt_read(void *ctx, float *dest, int count, int pos, int chan)
{
    const Pool::Virt *v = (const Pool::Virt *)ctx;
    int i = 0;
    while (i < count) {
        int vp = (pos + i) % v->vlen;
        if (vp < 0)
            vp += v->vlen;
        int j = SEQ_RING - 1;
        while (j > 0 && v->starts[j] > vp)
            j--;
        int end = j + 1 < SEQ_RING ? v->starts[j + 1] : v->vlen;
        int k = count - i < end - vp ? count - i : end - vp;
        int real = seq_unit_frame(v->q, v->clip, j) + (vp - v->starts[j]);
        while (k > 0) {
            int at = real % v->length;
            if (at < 0)
                at += v->length;
            int kk = v->length - at < k ? v->length - at : k;
            FW_PCM_READ(v->sample, dest + i, kk, at, chan);
            i += kk;
            real += kk;
            k -= kk;
        }
    }
    return count;
}
#endif

// The segment clock into Beats (h in [0, len)), only once it is off by more than HOST_SLACK frames
// the short way round: Beats runs its own position on at the ratio, so a clock that steps back a
// few frames (the tempo map's rounding, an external clock) leaves it be instead of moving it back.
#define HOST_SLACK 64

static void host_sync(BeatsVoice *b, double h, int len)
{
    double e = wrap_err(h - ((double)b->host_i + (double)b->host_f), len);
    if (e > HOST_SLACK || e < -HOST_SLACK) {
        b->host_i = (int32_t)h;
        b->host_f = (float)(h - (double)b->host_i);
    }
}

static void start_note(Slot *s, Pool *p, unsigned clip, unsigned lane, int mode, void *sample, int length,
                       int stereo)
{
    s->lane = lane;
    s->clip = clip;
    s->mode = mode;
    s->stamp = L64(lane, 0xE0);
    s->sample = sample;
    s->length = length;
    s->stereo = stereo;
    s->anchored = false;
    s->played = 0.;
    s->pending = true;
    s->seg = 0;
    if (mode == WARP_GRAIN) {
        gran_init(&s->e.g);
        gran_set(&s->e.g, 200, 1000, 9, 100, 1000, 300, 0, 1);
    } else {
        s->e.b.scratch = p->scratch;
        beats_init(&s->e.b);            /* the Lab's defaults, but the fill */
        s->e.b.fill = mode == WARP_FILL ? BEATS_LOOP : mode == WARP_CHOP ? BEATS_GATE : BEATS_RUNON;
    }
}

int rubber_lanes(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    Pool *p = pool();
    if (!p || p->magic != POOL_MAGIC || !clip || !lane || !mix || !ctx || !lane_on(lane))
        return 0;
    int mode = L8(clip, CLIP_FLAG);
    void *sample = *(void **)(clip + 0x24);
    if (!lane_mode(clip, mode) || !sample)
        return 0;
    int length = FW_PCM_LEN(sample);
    int stereo = FW_PCM_STEREO(sample) ? 1 : 0;
    if (length <= 0)
        return 0;
    Slot *s = find_slot(p, lane);
    if (!s)
        return 0;                       /* all slots busy: this lane plays stock grains */
    void *scratch = FW_BUF_SLOT((void *)ctx, 1);
    if (!scratch)
        return 0;
    FW_BUF_CLEAR(scratch);
    int n = FW_BUF_LEN((void *)mix);
    if (n <= 0)
        return 0;
    if (n > MAX_N)
        n = MAX_N;
    FW_BUF_SETLEN(scratch, n);
    float *dl = 0, *dr = 0;
    FW_BUF_PLANES(scratch, &dl, &dr);
    if (!dl)
        return 0;

    bool reset = s->lane != lane || s->clip != clip || s->mode != mode || s->stamp != L64(lane, 0xE0) ||
                 s->sample != sample || s->length != length || s->stereo != stereo;
    if (reset)
        start_note(s, p, clip, lane, mode, sample, length, stereo);
    void *transport = *(void **)ctx;
    if (!s->anchored && transport) {
        s->anchor = lane_launch_tick(lane, transport);
        s->anchored = true;
    }
    float ratio = LF(clip, 0xBB4);      /* tempo / clip tempo (repitch.S hook_rate) */
    if (!(ratio >= 0.25f))
        ratio = 0.25f;
    if (ratio > 4.f)
        ratio = 4.f;

#ifdef WARP_DECK_SEQ
    DeckSeq *q = &s->seq;
    if (reset)
        seq_start(q, clip, lane);
    if (q->on) {
        // Clip Deck: units since the launch, from the clock (else on at the ratio), and the units
        // deck.c chooses for them.
        double ticks, u = q->units;
        bool clocked = lane_ticks(lane, transport, s->anchor, &ticks);
        if (clocked)
            u = ticks / q->ut;
        if (u < 0.)
            u = 0.;
        seq_advance(q, clip, s->anchor, u, (double)(n + SEQ_LEAD) * ratio / q->spu);
        if (seq_cue_late(q, clip, s->anchor, u, SEQ_CUE_LATE * ratio / q->spu) >= 0)   // a late cue note
            seq_advance(q, clip, s->anchor, u, (double)(n + SEQ_LEAD) * ratio / q->spu);
        if (seq_ended(q, u)) {
            lane_end(clip, lane, 1);    // End with Loop off / Trigger, as deck_limit stops stock
            s->lane = 0;
            return 1;
        }
        double pos = seq_frame(q, clip, u), at = __builtin_fmod(pos, (double)length);
        if (at < 0.)
            at += length;
        int d = L32(lane, 0x138);
        if (d < 0)
            d = 0;
        int from = d < n ? d : n;
        L32(lane, 0x138) = d > n ? d - n : 0;
        float *mono_r = p->mono_r;
        // The note's reset goes with the first engine call that runs (not during a pre-delay).
        bool first = s->pending;
        if (s->mode == WARP_GRAIN) {
            GranVoice *g = &s->e.g;
            gran_src_buf = p->gran_src;
            g->play = pos;              // the grains read where the sequence is
            for (int i = from; i < n; i += SUB) {
                int k = n - i < SUB ? n - i : SUB;
                gran_process(g, sample, length, stereo, (GranReadFn)FW_PCM_READ, dl + i, dr ? dr + i : mono_r,
                             k, ratio, first && i == from);
            }
        } else {
            Pool::Virt *v = &p->virt;
            v->sample = sample;
            v->length = length;
            v->clip = clip;
            v->q = q;
            for (int j = 0; j < SEQ_RING; j++)
                v->starts[j] = (int32_t)((float)j * q->spu);
            v->vlen = (int)((float)SEQ_RING * q->spu);
            BeatsVoice *b = &s->e.b;
            if (!first) {
                double h = __builtin_fmod(u, (double)SEQ_RING) * q->spu;
                if (h >= v->vlen)
                    h -= v->vlen;
                host_sync(b, h, v->vlen);
            }
            for (int i = from; i < n; i += SUB) {
                int k = n - i < SUB ? n - i : SUB;
                beats_process(b, v, v->vlen, stereo, virt_read, v->starts, SEQ_RING, dl + i,
                              dr ? dr + i : mono_r, k, ratio, first && i == from);
            }
        }
        if (from < n)
            s->pending = false;
        if (!clocked)
            q->units = u + (double)(n - from) * ratio / q->spu;
        lane_gain(clip, lane, &s->gain, dl, dr, n, reset);
        if (!L32(lane, 0x138))
            FW_ENV_APPLY((void *)(lane + 0x258), dl, dr, n);
        lane_post_started(clip, lane, ctx, from);
        lane_post_pos(clip, ctx, at, length);
        FW_BUF_ENV((void *)(lane + 0x14c), scratch);
        FW_BUF_MIX((void *)mix, scratch);
        if (lane_finish(clip, lane, n))
            s->lane = 0;
        return 1;
    }
#endif
    // The segment clock: from the transport while it runs, else on at the ratio.
    double expect;
    bool clocked = lane_expect(clip, lane, transport, s->anchor, length, &expect);
    if (clocked && expect >= 0.)
        s->played = expect;
    if (!clip_loops(clip) && s->played >= length) {
        // Loop off, or Trigger: stock stops at the end of the file.
        lane_end(clip, lane, 1);
        s->lane = 0;
        return 1;
    }
    double at = __builtin_fmod(s->played, (double)length);
    if (at < 0.)
        at += length;
    int n_starts;
    const int32_t *starts = segments(p, clip, length, &n_starts);

    // Launch pre-delay (lane+0x138: where in the block the note starts): silence before it.
    int d = L32(lane, 0x138);
    if (d < 0)
        d = 0;
    int from = d < n ? d : n;
    L32(lane, 0x138) = d > n ? d - n : 0;
    float *mono_r = p->mono_r;
    bool first = s->pending;            // the note's reset: with the first engine call that runs
    if (s->mode == WARP_GRAIN) {
        GranVoice *g = &s->e.g;
        gran_src_buf = p->gran_src;
        if (clocked) {
            // Keep the scan on the clock: a big error (locate, launch offset) moves it at once.
            double err = wrap_err(at - __builtin_fmod(g->play, (double)length), length);
            if (err > 480. || err < -480.)
                g->play += err;
        }
        if (n_starts > 1) {
            // Sync: as the clock reaches each segment, the scan snaps to the clock (it was a block
            // late when it snapped to the segment's start: the clock is up to a block past it).
            int k = seg_of(starts, n_starts, at);
            if (k != s->seg) {
                g->play += wrap_err(at - __builtin_fmod(g->play, (double)length), length);
                s->seg = k;
            }
        }
        for (int i = from; i < n; i += SUB) {
            int k = n - i < SUB ? n - i : SUB;
            gran_process(g, sample, length, stereo, (GranReadFn)FW_PCM_READ, dl + i, dr ? dr + i : mono_r, k,
                         ratio, first && i == from);
        }
    } else {
        BeatsVoice *b = &s->e.b;
        if (!first)
            host_sync(b, at, length);   // the segment clock (only where segments start: heads run at 1x)
        for (int i = from; i < n; i += SUB) {
            int k = n - i < SUB ? n - i : SUB;
            beats_process(b, sample, length, stereo, (BeatsReadFn)FW_PCM_READ, starts, n_starts, dl + i,
                          dr ? dr + i : mono_r, k, ratio, first && i == from);
        }
    }
    if (from < n)
        s->pending = false;
    if (!clocked)
        s->played += (double)(n - from) * ratio;

    // As stock: Level and pan, the amp envelope once the pre-delay is over, the pad filter, the mix.
    lane_gain(clip, lane, &s->gain, dl, dr, n, reset);
    if (!L32(lane, 0x138))
        FW_ENV_APPLY((void *)(lane + 0x258), dl, dr, n);
    lane_post_started(clip, lane, ctx, from);
    lane_post_pos(clip, ctx, at, length);
    FW_BUF_ENV((void *)(lane + 0x14c), scratch);
    FW_BUF_MIX((void *)mix, scratch);
    if (lane_finish(clip, lane, n))
        s->lane = 0;
    return 1;
}
