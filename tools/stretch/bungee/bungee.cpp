// Warp "Bungee": Bungee Basic (MPL-2.0) rendering one Clip, in flash bank 2.
//
// Same ABI as Warp Rubber (tools/stretch/rubber/rubber.h), plus a background task: bank 1 calls
// rubber_boot() once per power-up, after b2pair.c has checked the payload; rubber_mix() from the
// per-lane render hook (repitch.S hook_lane) in the audio task, every block, for the one Clip that
// owns the voice; and rubber_work() from the leaf's own FreeRTOS task (b2pair.c rb_task, priority
// 5: under audio, over UI/disk/USB), woken by every rubber_mix. Each on its own stack.
//
// Why a task: on the unit Bungee costs ~40% of every audio block, and with the rest of the
// firmware's spikes on top the audio block overran (crackle). Now the audio task only does what
// has to happen in the block: the Clip's raw launch, copying Bungee's output from a ring, the
// stock lane's envelope / filter / end of voice. rubber_work renders whole grains into the ring
// when it gets the CPU, up to RING frames (~43 ms) ahead, so the stock spikes and Bungee's work no
// longer have to fit in the same 5.3 ms.
//
// A launch is instant: Bungee's first output frame comes out of its fourth grain (output trails by
// two grains, plus the preroll grain). Meanwhile the Clip plays unstretched from frame 0, and
// Bungee primes from frame START, where that raw playback will be at the handoff. The preroll hop
// runs at speed 1, so at START Bungee's phases match the clip; a short linear crossfade hands over,
// and from there Bungee runs at the Clip's ratio.
#include "rubber.h"

#include "bungee/Bungee.h"

#include <cmath>
#include <cstdint>

#define RB_MIN_RATIO 0.5f
#define RB_MAX_RATIO 2.0f
#define RB_BLOCKS_SEC 188    /* 48000 / 256: stats period */
#define SUB          256     /* frames per audio block */
#define HOP          512     /* Bungee's output hop at 48 kHz */
#define GRAIN_IN     4096    /* input frames per grain at pitch 1 (the analysis window) */
#define RING         2048    /* frames rendered ahead (~43 ms) */
#define START        2048    /* clip frame of the raw -> Bungee handoff (Bungee's ring frame 0) */
#define XF           128     /* crossfade frames at the handoff */
#define WAKE_GRAINS  3       /* grains per task wake at most: the UI and disk tasks get the CPU */
/* Pitch (clip+0xA80 and the lane's pitch mods): Bungee renders at speed / pitch and the audio side
 * reads its ring pitch frames per output frame, so the tempo stays and the pitch moves. Bungee's own
 * resampling would need ~65-80 KB more RAM; this needs none, but pitch up costs Bungee's task that
 * much more CPU (x1.5 at +7 st), so the range is -12..+7 semitones. */
#define PITCH_MIN    0.5f
#define PITCH_MAX    1.4983071f  /* 2^(7/12) */
/* The grain input buffer (4096 frames x 2 channels, 32 KB) in SRAM4's top half, which nothing else
 * uses on this line (folders, Warp flags, Mix, looper end below 0x38006400; RbVoice and diag at
 * 0x38007000-0x38007148). SRAM1-3 holds Bungee's own ~0.25 MB and cannot spare it. */
#define IN_BUF       ((float *)0x38008000u)

extern "C" {
extern uint32_t __b2_data_load[], __b2_data_start[], __b2_data_end[];
extern uint32_t __itcm_load[], __itcm_start[], __itcm_end[];
extern uint32_t __b2_bss_start[], __b2_bss_end[];
extern void (*__b2_init_array_start[])(void);
extern void (*__b2_init_array_end[])(void);
int rubber_boot(void);
void rubber_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx);
void rubber_work(void);
}

// The header bank 1 checks and calls through (rubber.h RbHeader, same layout). Length and sum
// are filled in by the build after the link; the loader writes this flash word last.
struct RbHeaderInit {
    uint32_t magic, id, length, sum;
    rb_boot_fn boot;
    rb_mix_fn mix;
    rb_work_fn work;
    const void *ext;      /* RbExt: 3.1.T's per-lane modes (lanes.cpp), or none */
};
#ifdef WARP_LANES
extern "C" const struct RbExtInit rb_ext;
extern "C" void lanes_boot(void);
#define RB_EXT &rb_ext
#else
#define RB_EXT nullptr
#endif
extern "C" __attribute__((used, section(".b2head")))
const RbHeaderInit rb_header = { RB_MAGIC, RB_ID, 0, 0, rubber_boot, rubber_mix, rubber_work, RB_EXT };

// Stock 3.1.9 functions and the stock lane's voice (envelope, Level and pan, pad feedback, end of
// voice): lane.h, shared with Fill / Slice / Grain (lanes.cpp).
#include "lane.h"

// The stock reader is not safe from two tasks (it bumps the stream cache's LRU counter and
// request ring without a lock; stock calls it from the audio task only). The leaf task suspends
// the scheduler around each call: the audio task cannot run in between, interrupts still can.
typedef void (*rtos_fn)(void);
typedef int (*rtos_ret_fn)(void);
#define FR_SUSPEND_ALL ((rtos_fn)0x08088D11u)
#define FR_RESUME_ALL  ((rtos_ret_fn)0x08088E7Du)

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DMB()      __asm__ volatile("dmb" ::: "memory")

#ifndef BG_PROFILE
#define BG_PROFILE 1   /* Warp label "B40a19s19u0": task CPU %, analysis, synthesis, underruns */
#endif

// ---- shared between the audio task (rubber_mix) and the leaf task (rubber_work) -------------
// The audio task asks for a launch by publishing its parameters and then a new req_seq; the leaf
// task answers by building the stretcher for it and publishing ring_seq = req_seq. Ring frames
// count from START (ring frame 0 is output frame START of the launch); the leaf task advances
// w_frames, the audio task r_frames. Each side writes only its own counters.
static float *ring_l, *ring_r;
static volatile uint32_t req_seq, ring_seq;
static volatile uint32_t w_frames, r_frames;
static volatile bool req_stop;              /* the voice ended: render nothing */
static void *volatile l_sample;              /* the launch's Clip */
static volatile int l_length, l_stereo;
static volatile float cur_ratio = 1.f;       /* the speed: the Clip's ratio, clock-corrected */
static volatile int l_base;                  /* the launch's first source frame (0, or a resync) */
// Which source frames each ring chunk covers (one per grain), for the playhead and the clock lock.
#define TRACK 8
static volatile uint32_t tr_start[TRACK];    /* ring frame of the chunk's first frame */
static volatile int tr_n[TRACK];
static volatile double tr_p0[TRACK], tr_p1[TRACK];
static volatile uint32_t tr_count;
static volatile uint32_t task_cyc, task_an, task_syn, underruns;   /* stats, for the label */

// ---- leaf task state ----------------------------------------------------------------------
static Bungee::Stretcher<Bungee::Basic> *bg;
static Bungee::Request req;
static float *const in_buf = IN_BUF;  /* planar grain input: [0, GRAIN_IN) left, then right */
static int win_begin;          /* clip frame (unwrapped) of in_buf[0], when win_ok */
static bool win_ok;            /* in_buf holds [win_begin, win_begin + GRAIN_IN) */
static bool unity_hop;         /* the preroll grain's hop runs at speed 1 */
static bool active;            /* rendering a launch */
static uint32_t cur_seq;       /* the launch the task is on */
static int bg_channels;        /* the stretcher's channel count: 2, or 1 for a mono Clip */
static void *t_sample;         /* the task's copy of the launch parameters */
static int t_length, t_stereo, t_base;

// ---- audio task state ---------------------------------------------------------------------
static uint32_t my_seq;        /* the launch the audio side plays */
static bool launching;         /* raw Clip until the handoff */
static bool playing;           /* a voice is on */
static int raw_pos;            /* raw Clip frames played since the launch */
static double rd_pos;          /* ring frame (fractional) the audio side reads next, after the launch */
static float a_pitch = 1.f;    /* the Clip's Pitch: the ring is read this many frames per output frame */
static bool priming;           /* a pitched launch: silence until Bungee's first frames are in */
static void *a_sample;         /* the Clip the audio side reads (raw launch, crossfade) */
static int a_length, a_stereo, a_base;
static double anchor_tick;     /* the clock tick at the launch's first frame */
static double lock_i;          /* the clock lock's integral term (removes a steady offset) */
static bool anchored;
static float xf_l[XF], xf_r[XF];
static LaneGain gain;          /* the pad's Level and pan ramps (lane.h) */
static uint32_t lbl_blocks, lbl_t0;

static RbVoice *voice() { return (RbVoice *)RB_VOICE; }

static void set_label(const char *s)
{
    char *d = voice()->label;
    int i = 0;
    for (; s[i] && i < 15; i++)
        d[i] = s[i];
    d[i] = 0;
}

static char *put_pct(char *p, uint32_t pct)
{
    if (pct > 999)
        pct = 999;
    if (pct >= 100)
        *p++ = (char)('0' + pct / 100);
    if (pct >= 10)
        *p++ = (char)('0' + pct / 10 % 10);
    *p++ = (char)('0' + pct % 10);
    return p;
}

// "B40a19s19u0": the leaf task's share of the CPU over the last second, of which analysis and
// synthesis, and the audio blocks it was late for (underruns: those frames played silent).
static void label_profile(uint32_t total, uint32_t an, uint32_t syn, uint32_t under)
{
    char s[16], *p = s;
    *p++ = 'B';
    p = put_pct(p, total);
    *p++ = 'a';
    p = put_pct(p, an);
    *p++ = 's';
    p = put_pct(p, syn);
    *p++ = 'u';
    p = put_pct(p, under);
    *p = 0;
    set_label(s);
}

// "Bungee 54%": rubber_mix's own share of the 256-frame budget, last second.
static void label_load(uint32_t pct)
{
    char s[16] = "Bungee ";
    int i = 7;
    if (pct > 999)
        pct = 999;
    if (pct >= 100)
        s[i++] = (char)('0' + pct / 100);
    if (pct >= 10)
        s[i++] = (char)('0' + pct / 10 % 10);
    s[i++] = (char)('0' + pct % 10);
    s[i++] = '%';
    s[i] = 0;
    set_label(s);
}

extern "C" int rb_heap_failed;   /* rt.c: an allocation did not fit */
extern "C" void rb_heap_mark(void), rb_heap_release(void);

// The stretcher for `channels`. The heap is rewound to the mark taken at boot first, so the
// previous one's memory is simply reused (rt.c). 2 channels is the largest and fits (boot).
static bool build(int channels)
{
    rb_heap_release();
    bg = new Bungee::Stretcher<Bungee::Basic>({48000, 48000}, channels);
    bg_channels = channels;
    if (rb_heap_failed || !bg) {
        bg = nullptr;
        set_label("Bungee! mem");
        return false;
    }
    return true;
}

int rubber_boot(void)
{
    // The hot code into ITCM first (bungee.ld .itcm): memcpy and the FFT live there. Volatile so
    // the compiler cannot turn these loops into a memcpy call before memcpy is in place.
    for (volatile uint32_t *s = __itcm_load, *d = __itcm_start; d < __itcm_end;)
        *d++ = *s++;
    __asm__ volatile("dsb\n isb");
    // Bank 2's .data from flash, .bss zeroed, then its C++ constructors (newlib, libstdc++).
    for (volatile uint32_t *s = __b2_data_load, *d = __b2_data_start; d < __b2_data_end;)
        *d++ = *s++;
    for (volatile uint32_t *d = __b2_bss_start; d < __b2_bss_end;)
        *d++ = 0;
    for (void (**f)(void) = __b2_init_array_start; f < __b2_init_array_end; f++)
        (*f)();
    cur_ratio = 1.f;
#ifdef WARP_LANES
    lanes_boot();
#endif

    DEMCR |= 1u << 24;   // TRCENA: DWT for the load figure
    DWT_CTRL |= 1u;      // CYCCNTENA

    ring_l = new float[2 * RING];
    ring_r = ring_l + RING;
    rb_heap_mark();
    if (rb_heap_failed || !ring_l || !build(2)) {
        // Constructed on null pointers somewhere: never use it.
        bg = nullptr;
        set_label("Bungee! mem");
        return 0;
    }
    set_label("Bungee");
    return 1;
}

// ---- clip reads -----------------------------------------------------------------------------

// Clip frames [pos, pos + n) of one channel: silence before frame 0 (a launch must not pull in
// the loop's tail), wrapping past the end (the Clip loops). `locked`: from the leaf task, with
// the scheduler suspended around each stock read.
static void read_chan(void *sample, int length, int pos, int n, float *dest, int chan, bool locked)
{
    int i = 0;
    while (i < n) {
        int p = pos + i, k;
        if (p < 0) {
            k = -p < n - i ? -p : n - i;
            while (k--)
                dest[i++] = 0.f;
            continue;
        }
        p %= length;
        k = length - p;
        if (k > n - i)
            k = n - i;
        if (locked)
            FR_SUSPEND_ALL();
        FW_PCM_READ(sample, dest + i, k, p, chan);
        if (locked)
            FR_RESUME_ALL();
        i += k;
    }
}

// The audio side's raw Clip (launch and crossfade): the audio task, as stock reads.
static void read_raw(int pos, int n, float *l, float *r)
{
    read_chan(a_sample, a_length, a_base + pos, n, l, 0, false);
    if (a_stereo)
        read_chan(a_sample, a_length, a_base + pos, n, r, 1, false);
    else
        for (int i = 0; i < n; i++)
            r[i] = l[i];
}

// ---- leaf task: Bungee -------------------------------------------------------------------------

// The grain's input window into in_buf. Consecutive grains overlap by GRAIN_IN minus the input
// hop (~512 x ratio), so the frames already there move down and only the new ones are read: each
// clip frame goes through the stock reader once instead of ~8 times.
static void fill_window(int begin, int k)
{
    int keep = win_ok ? win_begin + GRAIN_IN - begin : 0;
    if (k != GRAIN_IN || begin < win_begin || keep <= 0 || keep > GRAIN_IN)
        keep = 0;
    if (keep > 0 && keep < GRAIN_IN) {
        int shift = GRAIN_IN - keep;
        for (int c = 0; c < bg_channels; c++)
            for (int i = 0; i < keep; i++)
                in_buf[c * GRAIN_IN + i] = in_buf[c * GRAIN_IN + shift + i];
    }
    if (keep < k) {
        read_chan(t_sample, t_length, begin + keep, k - keep, in_buf + keep, 0, true);
        if (bg_channels == 2) {
            if (t_stereo)
                read_chan(t_sample, t_length, begin + keep, k - keep, in_buf + GRAIN_IN + keep, 1, true);
            else
                for (int i = keep; i < k; i++)
                    in_buf[GRAIN_IN + i] = in_buf[i];
        }
    }
    win_begin = begin;
    win_ok = k == GRAIN_IN;
}

// One grain: analysis, synthesis, and its output (from START on) into the ring.
static void grain()
{
    uint32_t t0 = DWT_CYCCNT;
    Bungee::InputChunk c = bg->specifyGrain(req);
    int k = c.end - c.begin;
    if (k > GRAIN_IN)
        k = GRAIN_IN;
    fill_window(c.begin, k);
    bg->analyseGrain(in_buf, GRAIN_IN);
    uint32_t t1 = DWT_CYCCNT;
    task_an += t1 - t0;

    Bungee::OutputChunk o;
    bg->synthesiseGrain(o);
    if (unity_hop) {
        unity_hop = false;
        req.speed = 1.;
        bg->next(req);
    } else {
        req.speed = cur_ratio;
        bg->next(req);
    }
    req.speed = cur_ratio;
    task_syn += DWT_CYCCNT - t1;

    double p0 = o.request[Bungee::OutputChunk::begin]->position;
    if (std::isnan(p0) || p0 < t_base + START)
        return;   // preroll and the grains before the handoff
    const float *r = bg_channels == 2 ? o.data + o.channelStride : o.data;
    uint32_t w = w_frames;
    uint32_t t = tr_count % TRACK;
    tr_start[t] = w;
    tr_n[t] = o.frameCount;
    tr_p0[t] = p0;
    tr_p1[t] = o.request[Bungee::OutputChunk::end]->position;
    DMB();
    tr_count = tr_count + 1;
    for (int i = 0; i < o.frameCount; i++) {
        ring_l[(w + i) % RING] = o.data[i];
        ring_r[(w + i) % RING] = r[i];
    }
    DMB();
    w_frames = w + (uint32_t)o.frameCount;
}

// A new launch: the stretcher for its channel count, Bungee primed from START with its first
// hop at speed 1 (phases match the raw Clip at the handoff).
static void start_launch()
{
    t_sample = l_sample;
    t_length = l_length;
    t_stereo = l_stereo;
    t_base = l_base;
    int channels = t_stereo ? 2 : 1;
    if (channels != bg_channels && !build(channels)) {
        active = false;
        return;
    }
    req = {};
    req.position = t_base + START;
    req.speed = 1.;
    req.pitch = 1.;
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
    bg->preroll(req);
    unity_hop = true;
    win_ok = false;
    w_frames = 0;
    tr_count = 0;
    active = true;
}

void rubber_work(void)
{
    uint32_t t0 = DWT_CYCCNT;
    uint32_t seq = req_seq;
    DMB();
    if (seq != cur_seq) {
        cur_seq = seq;
        if (req_stop || !l_sample || !bg)
            active = false;
        else
            start_launch();
        DMB();
        ring_seq = seq;
    }
    for (int g = 0; active && bg && g < WAKE_GRAINS; g++) {
        if (w_frames - r_frames > (uint32_t)(RING - HOP))
            break;   // a grain's output would overwrite frames not played yet
        if (req_seq != cur_seq)
            break;   // a new launch came in: next wake
        grain();
    }
    task_cyc += DWT_CYCCNT - t0;
}

// ---- audio task: the Clip's lanes, launch, ring --------------------------------------------

// The lane Bungee renders: the newest active one (largest start tick, lane+0xE0). A relaunch
// starts the new note on the other lane while the old one releases; Bungee is one voice, so the
// old lane ends at once instead of hanging on unrendered.
static unsigned pick_lane(unsigned clip)
{
    unsigned a = clip + LANE_A, b = clip + LANE_B;
    bool on_a = lane_on(a), on_b = lane_on(b);
    if (on_a && on_b) {
        int64_t ta = *(volatile int64_t *)(a + 0xE0), tb = *(volatile int64_t *)(b + 0xE0);
        unsigned old = ta >= tb ? b : a;
        lane_end(clip, old, true);
        return old == a ? b : a;
    }
    return on_a ? a : on_b ? b : 0;
}

// One block's Bungee audio, n frames into l/r.
static void play(float *l, float *r, int n)
{
    int i, m;
    if (launching) {
        // The raw Clip plays until START, then on until Bungee's ring has that frame (if the leaf
        // task is late, its first frames are dropped to stay in time; the phases then match a
        // little less well at the crossfade).
        m = START - raw_pos;
        if (m > n)
            m = n;
        if (m > 0) {
            read_raw(raw_pos, m, l, r);
            raw_pos += m;
            l += m;
            r += m;
            n -= m;
        }
        if (n == 0)
            return;
        uint32_t skip = (uint32_t)(raw_pos - START);
        if (ring_seq != my_seq || w_frames < skip + (uint32_t)n) {
            read_raw(raw_pos, n, l, r);
            raw_pos += n;
            return;
        }
        r_frames = skip;
        launching = false;
        int k = n < XF ? n : XF;
        read_raw(raw_pos, k, xf_l, xf_r);
        for (i = 0; i < n; i++) {
            l[i] = ring_l[(skip + i) % RING];
            r[i] = ring_r[(skip + i) % RING];
        }
        for (i = 0; i < k; i++) {
            float a = (i + 0.5f) / XF;
            l[i] = a * l[i] + (1.f - a) * xf_l[i];
            r[i] = a * r[i] + (1.f - a) * xf_r[i];
        }
        DMB();
        r_frames = skip + (uint32_t)n;
        rd_pos = r_frames;
        return;
    }
    if (a_pitch == 1.f && rd_pos == (double)(uint32_t)rd_pos) {
        uint32_t rd = (uint32_t)rd_pos, avail = ring_seq == my_seq ? w_frames - rd : 0;
        m = avail < (uint32_t)n ? (int)avail : n;
        DMB();
        for (i = 0; i < m; i++) {
            l[i] = ring_l[(rd + i) % RING];
            r[i] = ring_r[(rd + i) % RING];
        }
        rd_pos = rd + (uint32_t)m;
    } else {
        // At a pitch: the ring at rd_pos, rd_pos + pitch, ... (cubic Hermite over 4 frames).
        uint32_t w = ring_seq == my_seq ? w_frames : 0;
        DMB();
        for (i = 0; i < n; i++) {
            uint32_t k = (uint32_t)rd_pos;
            if (k < 1u || k + 3u > w) {
                if (k == 0u && w >= 3u) {          // the first frame: no frame before it
                    l[i] = ring_l[0];
                    r[i] = ring_r[0];
                    rd_pos += a_pitch;
                    continue;
                }
                break;
            }
            float t = (float)(rd_pos - k);
            const float *rings[2] = { ring_l, ring_r };
            float *dst[2] = { l, r };
            for (int c = 0; c < 2; c++) {
                const float *q = rings[c];
                float y0 = q[(k - 1u) % RING], y1 = q[k % RING], y2 = q[(k + 1u) % RING], y3 = q[(k + 2u) % RING];
                float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
                float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                dst[c][i] = ((c3 * t + c2) * t + c1) * t + y1;
            }
            rd_pos += a_pitch;
        }
        m = i;
    }
    for (i = m; i < n; i++)
        l[i] = r[i] = 0.f;   // the leaf task was late: silence, counted (not while a pitched launch primes)
    if (m > 0)
        priming = false;
    if (m < n && !priming)
        underruns++;
    DMB();
    uint32_t keep = (uint32_t)rd_pos;
    r_frames = keep > 0u ? keep - 1u : 0u;   // the frame before is still read (cubic)
}

// The source frame (unwrapped) of the frame the audio side plays next.
static double src_pos()
{
    if (launching)
        return a_base + raw_pos;
    double r = rd_pos;
    uint32_t cnt = tr_count;
    for (uint32_t i = 0; i < TRACK && i < cnt; i++) {
        uint32_t t = (cnt - 1 - i) % TRACK;
        double st = tr_start[t];
        if (r >= st && r - st <= (double)tr_n[t] && tr_n[t] > 0)
            return tr_p0[t] + (tr_p1[t] - tr_p0[t]) * (r - st) / tr_n[t];
    }
    return a_base + START + r * cur_ratio;
}

static void launch_voice(void *sample, int length, int stereo, int base)
{
    a_sample = sample;
    a_length = length;
    a_stereo = stereo;
    a_base = base;
    l_sample = sample;
    l_length = length;
    l_stereo = stereo;
    l_base = base;
    req_stop = false;
    raw_pos = 0;
    r_frames = 0;
    rd_pos = 0.;
    launching = true;
    priming = false;
    playing = true;
    DMB();
    req_seq = ++my_seq;
}

// A launch at a pitch: no raw Clip first (it would be at the wrong pitch for the crossfade); Bungee
// primes from `base` itself, and its output starts ~43 ms later.
static void launch_pitched(void *sample, int length, int stereo, int base)
{
    launch_voice(sample, length, stereo, base - START);
    launching = false;
    priming = true;
}

static void launch_at(void *sample, int length, int stereo, int base)
{
    if (a_pitch == 1.f)
        launch_voice(sample, length, stereo, base);
    else
        launch_pitched(sample, length, stereo, base);
}

static void stop_voice()
{
    playing = false;
    req_stop = true;
    DMB();
    req_seq = ++my_seq;
}

void rubber_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    RbVoice *v = voice();
    if (!bg || !clip || !lane || !mix || !ctx)
        return;
    // A launch on this Clip took the voice from another (choke switch): that Clip's lanes end now,
    // as a choke's fast release would, so they neither hang unrendered nor play stock grains.
    unsigned prev = v->prev_owner;
    if (prev && prev != clip) {
        for (unsigned o = LANE_A; o <= LANE_B; o += LANE_B - LANE_A)
            if (L8(prev + o, 0))
                lane_end(prev, prev + o, true);
    }
    v->prev_owner = 0;
    lane = pick_lane(clip);
    if (!lane)
        return;
    void *sample = *(void **)(clip + 0x24);
    if (!sample)
        return;
    int length = FW_PCM_LEN(sample);
    int stereo = FW_PCM_STEREO(sample) ? 1 : 0;
    if (length <= 0)
        return;
    void *scratch = FW_BUF_SLOT((void *)ctx, 1);
    if (!scratch)
        return;
    FW_BUF_CLEAR(scratch);
    int n = FW_BUF_LEN((void *)mix);
    if (n <= 0)
        return;
    if (n > 2048)
        n = 2048;
    FW_BUF_SETLEN(scratch, n);
    float *dl = 0, *dr = 0;
    FW_BUF_PLANES(scratch, &dl, &dr);
    if (!dl)
        return;

    float want = *(float *)(clip + 0xBB4);   // tempo / clip tempo (repitch.S hook_rate)
    float ratio = want;
    if (!(ratio >= RB_MIN_RATIO))
        ratio = RB_MIN_RATIO;
    if (ratio > RB_MAX_RATIO)
        ratio = RB_MAX_RATIO;
    void *transport = *(void **)ctx;
    bool loops = clip_loops(clip);

    int reset = v->reset_req;
    v->reset_req = 0;
    unsigned char a0 = *(unsigned char *)(clip + 0x260), a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !v->prev_active)
        reset = 1;
    v->prev_active = (a0 || a1) ? 1 : 0;
    // The Clip's Pitch (param 0x5C, milli-semitones at clip+0xA80) and the lane's pitch mods
    // (octaves), as stock's grain reader takes them.
    float pitch = std::exp2((float)L32(clip, 0xA80) / 12000.f + FW_PARAM_VAL((void *)(lane + 0x94)));
    if (!(pitch >= PITCH_MIN))
        pitch = PITCH_MIN;
    if (pitch > PITCH_MAX)
        pitch = PITCH_MAX;
    if (pitch > 0.9999f && pitch < 1.0001f)
        pitch = 1.f;
    a_pitch = pitch;
    bool snap = false;               /* a new note: Level and pan start at their value */
    if (reset || !playing || sample != a_sample || stereo != a_stereo) {
        launch_at(sample, length, stereo, 0);
        anchored = false;
        lock_i = 0;
        snap = true;
    }
    if (!anchored && transport) {
        anchor_tick = lane_launch_tick(lane, transport);
        anchored = true;
    }

    // Clock lock: where stock would be in the file now, against where Bungee is; the speed is
    // nudged (at most 2 %, no pitch change) so a long sample stays on the beat. A big jump (locate,
    // external clock) relaunches there instead.
    double speed_now = ratio, expect;
    if (!launching && !priming && ratio == want &&
        lane_expect(clip, lane, transport, anchor_tick, length, &expect)) {
        double err = wrap_err(expect - src_pos(), length);
        if (err > 4800. || err < -4800.) {
            double at = std::fmod(expect, (double)length);
            if (at < 0)
                at += length;
            launch_at(sample, length, stereo, (int)at);
        } else {
            // PI on the source-frame error, tuned with the ring's ~8-block delay in the loop
            // (bench): 2 ms of error asks the full 2 %, the integral takes over a steady
            // difference (a clip tempo a hair off, an external clock) and settles within ~1 s. A
            // speed change, not pitch: 2 % for a moment is inaudible. No integration while the
            // output is at its limit (no wind-up after a launch's raw-start offset).
            double c = err * 1e-4 + lock_i;
            if (c > 0.02)
                c = 0.02;
            else if (c < -0.02)
                c = -0.02;
            else
                lock_i += err * 5e-7;
            if (lock_i > 0.01)
                lock_i = 0.01;
            if (lock_i < -0.01)
                lock_i = -0.01;
            speed_now = ratio * (1. + c);
        }
    }
    cur_ratio = (float)(speed_now / pitch);   // Bungee's speed: the ring is read `pitch` times faster

    // Launch pre-delay (lane+0x138: where in the block the note starts): silence before it.
    static float mono_r[SUB];   /* a buffer without a right plane: play() still writes one */
    int d = L32(lane, 0x138);
    if (d < 0)
        d = 0;
    int from = d < n ? d : n;
    L32(lane, 0x138) = d > n ? d - n : 0;
    for (int at = from; at < n; at += SUB) {
        int k = n - at < SUB ? n - at : SUB;
        play(dl + at, dr ? dr + at : mono_r, k);
    }

    // As stock: the pad's Level and pan (the grain reader's step), the amp envelope once the
    // pre-delay is over (R as the buffer has it, as stock passes it), then the pad filter, the mix.
    lane_gain(clip, lane, &gain, dl, dr, n, snap);
    if (!L32(lane, 0x138))
        FW_ENV_APPLY((void *)(lane + 0x258), dl, dr, n);

    // Pad feedback, as stock's lane render posts it: "started" once a quantised launch's delay is
    // over, the playhead every block.
    lane_post_started(clip, lane, ctx, from);
    double here = src_pos();
    if (!loops && here >= length) {
        // Loop off, or Trigger: stock stops at the end of the file. Bungee reads wrap, so stop here.
        lane_end(clip, lane, true);
        stop_voice();
        set_label("Bungee");
        lbl_blocks = 0;
        return;
    }
    {
        double at = std::fmod(here, (double)length);
        if (at < 0)
            at += length;
        lane_post_pos(clip, ctx, at, length);
    }
    FW_BUF_ENV((void *)(lane + 0x14c), scratch);
    FW_BUF_MIX((void *)mix, scratch);

    // End of voice, as stock: the envelope finished (stop, note-off, mute), a quantised release
    // came due, or the grains were stopped.
    if (lane_finish(clip, lane, n)) {
        stop_voice();
        set_label("Bungee");
        lbl_blocks = 0;
        return;
    }

    // The label, once a second: the leaf task's CPU share (DWT cycles over wall time).
    if (lbl_blocks == 0) {
        lbl_t0 = DWT_CYCCNT;
        task_cyc = task_an = task_syn = underruns = 0;
    }
    if (++lbl_blocks > RB_BLOCKS_SEC) {
        uint32_t wall = DWT_CYCCNT - lbl_t0, per = wall / 100u + 1u;
#if BG_PROFILE
        label_profile(task_cyc / per, task_an / per, task_syn / per, underruns);
#else
        label_load(task_cyc / per);
#endif
        if (underruns > RB_BLOCKS_SEC / 8u) {
            // Late for more than an eighth of the blocks: too slow for this unit's load. Off, as
            // the CPU guard would (stock grains from the next block).
            v->state = (v->state & ~0xffu) | RB_ST_SLOW;
            set_label("Bungee! cpu");
        }
        lbl_blocks = 0;
    }
}
