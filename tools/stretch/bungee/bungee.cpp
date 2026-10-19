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
    uint32_t reserved;
};
extern "C" __attribute__((used, section(".b2head")))
const RbHeaderInit rb_header = { RB_MAGIC, RB_ID, 0, 0, rubber_boot, rubber_mix, rubber_work, 0 };

// Stock 3.1.9 functions, as used by Warp Rubber / Capi.
typedef int (*pcm_read_fn)(void *, float *, int, int, int);
typedef int (*pcm_len_fn)(void *);
typedef int (*pcm_st_fn)(void *);
typedef void *(*buf_slot_fn)(void *, int);
typedef void (*buf_clear_fn)(void *);
typedef void (*buf_setlen_fn)(void *, int);
typedef int (*buf_len_fn)(void *);
typedef void (*buf_planes_fn)(void *, float **, float **);
typedef void (*buf_env_fn)(void *, void *);
typedef void (*buf_mix_fn)(void *, void *);
#define FW_PCM_READ   ((pcm_read_fn)0x08063635u)
#define FW_PCM_LEN    ((pcm_len_fn)0x080636f1u)
#define FW_PCM_STEREO ((pcm_st_fn)0x08063791u)
#define FW_BUF_SLOT   ((buf_slot_fn)0x0805f3a5u)
#define FW_BUF_CLEAR  ((buf_clear_fn)0x0804d8ddu)
#define FW_BUF_LEN    ((buf_len_fn)0x0804d8e9u)
#define FW_BUF_SETLEN ((buf_setlen_fn)0x0804d8d1u)
#define FW_BUF_PLANES ((buf_planes_fn)0x0804d9c1u)
#define FW_BUF_ENV    ((buf_env_fn)0x080762d9u)
#define FW_BUF_MIX    ((buf_mix_fn)0x0804da6du)
// The stock Clip lane's voice: what FUN_080667B8 (the per-lane render our hook replaces) does around
// its grains. STOP, note-off and mute only arm a release or stop the grains; the lane render runs
// the amp envelope, sees it finish, and deactivates the lane. Without this a Bungee Clip played on.
typedef void (*env_apply_fn)(void *, float *, float *, int);
typedef void (*obj_fn)(void *);
#define FW_ENV_APPLY  ((env_apply_fn)0x0806a6c1u)   /* (lane+0x258, L, R, n): gains in place, advances stages */
#define FW_ENV_REL    ((obj_fn)0x0806a63du)         /* release (stages 1-3 -> 4) */
#define FW_ENV_FAST   ((obj_fn)0x0806a699u)         /* fast release, 256 samples */
#define FW_ENV_RESET  ((obj_fn)0x0806a5d9u)
#define FW_AUX_RESET  ((obj_fn)0x0806ef39u)         /* lane+0xF0 */
#define FW_FILT_RESET ((obj_fn)0x08076705u)         /* lane+0x14C, the pad filter */
#define LANE_A        0x260u                        /* clip+0x260 and clip+0x670: a Clip's two lanes */
#define LANE_B        0x670u
#define L8(l, o)   (*(volatile uint8_t *)((l) + (o)))
#define L16(l, o)  (*(volatile uint16_t *)((l) + (o)))
#define L32(l, o)  (*(volatile int32_t *)((l) + (o)))

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
static volatile float cur_ratio = 1.f;       /* the Clip's ratio, every block */
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
static int t_length, t_stereo;

// ---- audio task state ---------------------------------------------------------------------
static uint32_t my_seq;        /* the launch the audio side plays */
static bool launching;         /* raw Clip until the handoff */
static bool playing;           /* a voice is on */
static int raw_pos;            /* raw Clip frames played since the launch */
static void *a_sample;         /* the Clip the audio side reads (raw launch, crossfade) */
static int a_length, a_stereo;
static float xf_l[XF], xf_r[XF];
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
    read_chan(a_sample, a_length, pos, n, l, 0, false);
    if (a_stereo)
        read_chan(a_sample, a_length, pos, n, r, 1, false);
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
    if (std::isnan(p0) || p0 < START)
        return;   // preroll and the grains before the handoff
    const float *r = bg_channels == 2 ? o.data + o.channelStride : o.data;
    uint32_t w = w_frames;
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
    int channels = t_stereo ? 2 : 1;
    if (channels != bg_channels && !build(channels)) {
        active = false;
        return;
    }
    req = {};
    req.position = START;
    req.speed = 1.;
    req.pitch = 1.;
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
    bg->preroll(req);
    unity_hop = true;
    win_ok = false;
    w_frames = 0;
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

static bool lane_on(unsigned lane) { return L8(lane, 0) && !L8(lane, 0x148); }

// Stock's lane deactivate (FUN_080667B8 0x08066D2A-0x08066E78): the grains in the lane's list go
// back to the Clip's four slots, the lane's fields clear, its envelope, aux and filter reset.
static void lane_end(unsigned clip, unsigned lane, bool free_grains)
{
    if (free_grains) {
        int count = L32(lane, 0xD8);
        for (int g = 0; g < count && g < 4; g++) {
            int idx = L32(lane, 0xC8 + 4 * g);
            if (idx < 0 || idx > 3)
                continue;
            unsigned gr = clip + (unsigned)idx * 0x70u;
            L16(gr, 0x30) = 0;
            L32(gr, 0x68) = 0;
            L8(gr, 0x6C) = 0;
            *(volatile float *)(gr + 0x48) = 1.f;
            L32(gr, 0x4C) = 0;
            L32(gr, 0x50) = 0;
            L32(gr, 0x54) = 0x100;
            L32(gr, 0x58) = -1;
            L32(gr, 0x5C) = 0x100;
            L32(gr, 0x60) = 0;
            L32(gr, 0x64) = 0;
            for (int o = 0x38; o < 0x48; o += 4)
                L32(gr, o) = 0;
        }
        L32(lane, 0xD8) = 0;
    }
    L8(lane, 0) = 0;
    L32(lane, 4) = 0;
    L16(lane, 8) = 0;
    for (int o = 0xC8; o <= 0xD4; o += 4)
        L32(lane, o) = -1;
    L32(lane, 0x130) = 0;
    L16(lane, 0x134) = 0;
    L8(lane, 0x148) = 0;
    for (int o = 0xE0; o < 0xF0; o += 4)
        L32(lane, o) = 0;
    for (int o = 0x138; o < 0x148; o += 4)
        L32(lane, o) = 0;
    FW_ENV_RESET((void *)(lane + 0x258));
    FW_AUX_RESET((void *)(lane + 0xF0));
    FW_FILT_RESET((void *)(lane + 0x14C));
}

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
        return;
    }
    uint32_t rd = r_frames, avail = ring_seq == my_seq ? w_frames - rd : 0;
    m = avail < (uint32_t)n ? (int)avail : n;
    DMB();
    for (i = 0; i < m; i++) {
        l[i] = ring_l[(rd + i) % RING];
        r[i] = ring_r[(rd + i) % RING];
    }
    for (; i < n; i++)
        l[i] = r[i] = 0.f;   // the leaf task was late: silence, counted
    if (m < n)
        underruns++;
    DMB();
    r_frames = rd + (uint32_t)m;
}

static void launch_voice(void *sample, int length, int stereo)
{
    a_sample = sample;
    a_length = length;
    a_stereo = stereo;
    l_sample = sample;
    l_length = length;
    l_stereo = stereo;
    req_stop = false;
    raw_pos = 0;
    r_frames = 0;
    launching = true;
    playing = true;
    DMB();
    req_seq = ++my_seq;
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

    float ratio = *(float *)(clip + 0xBB4);   // tempo / clip tempo (repitch.S hook_rate)
    if (!(ratio >= RB_MIN_RATIO))
        ratio = RB_MIN_RATIO;
    if (ratio > RB_MAX_RATIO)
        ratio = RB_MAX_RATIO;
    cur_ratio = ratio;

    int reset = v->reset_req;
    v->reset_req = 0;
    unsigned char a0 = *(unsigned char *)(clip + 0x260), a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !v->prev_active)
        reset = 1;
    v->prev_active = (a0 || a1) ? 1 : 0;
    if (reset || !playing || sample != a_sample || stereo != a_stereo)
        launch_voice(sample, length, stereo);

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

    // As stock: the amp envelope once the pre-delay is over (R as the buffer has it, as stock
    // passes it), then the pad filter, then the mix.
    if (!L32(lane, 0x138))
        FW_ENV_APPLY((void *)(lane + 0x258), dl, dr, n);
    FW_BUF_ENV((void *)(lane + 0x14c), scratch);
    FW_BUF_MIX((void *)mix, scratch);

    // End of voice, as stock: the envelope finished (stop, note-off, mute), a quantised release
    // came due, or the grains were stopped.
    bool ended = false;
    if (L8(lane, 0x25C) == 5) {
        lane_end(clip, lane, true);
        ended = true;
    } else {
        if (L8(lane, 0x134)) {
            if (L32(lane, 0x130) >= n) {
                L32(lane, 0x130) -= n;
            } else {
                if (L8(lane, 0x135))
                    FW_ENV_FAST((void *)(lane + 0x258));
                else
                    FW_ENV_REL((void *)(lane + 0x258));
                FW_ENV_REL((void *)(lane + 0x258));
                L8(lane, 0x134) = 0;
            }
        }
        if (L8(lane, 9) && L32(lane, 0xD8) == 0) {
            lane_end(clip, lane, false);
            ended = true;
        }
    }
    if (ended) {
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
