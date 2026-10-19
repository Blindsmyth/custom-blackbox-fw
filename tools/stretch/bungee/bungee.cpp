// Warp "Bungee": Bungee Basic (MPL-2.0) rendering one Clip, in flash bank 2.
//
// Same ABI as Warp Rubber (tools/stretch/rubber/rubber.h): bank 1 calls rubber_boot() once per
// power-up, after b2pair.c has checked the payload, and rubber_mix() from the per-lane render hook
// (repitch.S hook_lane) for the one Clip that owns the voice, both on the leaf's own stack.
// Everything allocated comes from SRAM1-3 (rt.c); .data and .bss live there too (bungee.ld).
//
// Bungee works in grains of 4096 frames with a fixed 512-frame output hop, so its cost per output
// frame is the same at every tempo. Each grain is split over two 256-frame blocks: specifyGrain +
// analyseGrain in one, synthesiseGrain in the next, so no block pays for a whole grain.
//
// A launch is instant: Bungee's first output frame comes out of its fourth grain (output trails by
// two grains, plus the preroll grain), eight half-grain blocks (~43 ms). Meanwhile the Clip plays
// unstretched from frame 0, and Bungee primes from frame START, where that raw playback will be
// when priming is done. The preroll hop runs at speed 1, so at START Bungee's phases match the
// clip; a short linear crossfade hands over, and from there Bungee runs at the Clip's ratio.
#include "rubber.h"

#include "bungee/Bungee.h"

#include <cmath>
#include <cstdint>

#define RB_MIN_RATIO 0.5f
#define RB_MAX_RATIO 2.0f
#define RB_BLOCKS_SEC 188    /* 48000 / 256: stats period */
#define SUB          256     /* frames per sub-block: one half-grain of work each */
#define HOP          512     /* Bungee's output hop at 48 kHz */
#define GRAIN_IN     4096    /* input frames per grain at pitch 1 (the analysis window) */
#define PEND_MAX     (SUB + HOP)
#define PRIME_HALVES 8
#define START        (PRIME_HALVES * SUB)   /* clip frame of the raw -> Bungee handoff */
#define XF           128     /* crossfade frames at the handoff */
/* The grain input buffer (4096 frames x 2 channels, 32 KB) in SRAM4's top half, which nothing else
 * uses on this line (folders, Warp flags, Mix, looper end below 0x38006400; RbVoice and diag at
 * 0x38007000-0x38007140). SRAM1-3 holds Bungee's own ~0.25 MB and cannot spare it. */
#define IN_BUF       ((float *)0x38008000u)

extern "C" {
extern uint32_t __b2_data_load[], __b2_data_start[], __b2_data_end[];
extern uint32_t __itcm_load[], __itcm_start[], __itcm_end[];
extern uint32_t __b2_bss_start[], __b2_bss_end[];
extern void (*__b2_init_array_start[])(void);
extern void (*__b2_init_array_end[])(void);
int rubber_boot(void);
void rubber_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx);
}

// The header bank 1 checks and calls through (rubber.h RbHeader, same layout). Length and sum
// are filled in by the build after the link; the loader writes this flash word last.
struct RbHeaderInit {
    uint32_t magic, id, length, sum;
    rb_boot_fn boot;
    rb_mix_fn mix;
    uint32_t reserved[2];
};
extern "C" __attribute__((used, section(".b2head")))
const RbHeaderInit rb_header = { RB_MAGIC, RB_ID, 0, 0, rubber_boot, rubber_mix, { 0, 0 } };

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

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

static Bungee::Stretcher<Bungee::Basic> *bg;
static Bungee::Request req;
static float *const in_buf = IN_BUF;  /* planar grain input: [0, GRAIN_IN) left, then right */
static float *pend_l, *pend_r; /* synthesised frames not yet played */
static int pend_n;
static bool analysed;          /* next grain analysed, not yet synthesised */
static bool primed;            /* Bungee has output on hand: the handoff can happen */
static bool launch;            /* playing the raw Clip while Bungee primes */
static bool unity_hop;         /* the preroll grain's hop runs at speed 1 */
static bool started;           /* a launch has set the request up */
static double speed;           /* the Clip's ratio, applied from the handoff on */
static int raw_pos;            /* raw Clip frames played since the launch */
static float xf_l[XF], xf_r[XF];
static uint32_t acc_cyc, peak_cyc, n_blocks;

// The Clip being rendered, for the reads Bungee asks for.
static void *cur_sample;
static int cur_length, cur_stereo;

static RbVoice *voice() { return (RbVoice *)RB_VOICE; }

static void set_label(const char *s)
{
    char *d = voice()->label;
    int i = 0;
    for (; s[i] && i < 15; i++)
        d[i] = s[i];
    d[i] = 0;
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

    DEMCR |= 1u << 24;   // TRCENA: DWT for the load figure
    DWT_CTRL |= 1u;      // CYCCNTENA

    bg = new Bungee::Stretcher<Bungee::Basic>({48000, 48000}, 2);
    pend_l = new float[2 * PEND_MAX];
    pend_r = pend_l + PEND_MAX;
    if (rb_heap_failed || !bg || !pend_l) {
        // Constructed on null pointers somewhere: never use it.
        bg = nullptr;
        set_label("Bungee! mem");
        return 0;
    }
    set_label("Bungee");
    return 1;
}

// Clip frames [pos, pos + n) of one channel: silence before frame 0 (a launch must not pull in
// the loop's tail), wrapping past the end (the Clip loops).
static void read_chan(int pos, int n, float *dest, int chan)
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
        p %= cur_length;
        k = cur_length - p;
        if (k > n - i)
            k = n - i;
        FW_PCM_READ(cur_sample, dest + i, k, p, chan);
        i += k;
    }
}

static void read_clip(int pos, int n, float *l, float *r)
{
    read_chan(pos, n, l, 0);
    if (cur_stereo)
        read_chan(pos, n, r, 1);
    else
        for (int i = 0; i < n; i++)
            r[i] = l[i];
}

static void analyse()
{
    Bungee::InputChunk c = bg->specifyGrain(req);
    int k = c.end - c.begin;
    if (k > GRAIN_IN)
        k = GRAIN_IN;
    read_clip(c.begin, k, in_buf, in_buf + GRAIN_IN);
    bg->analyseGrain(in_buf, GRAIN_IN);
    analysed = true;
}

static void synthesise()
{
    Bungee::OutputChunk o;
    bg->synthesiseGrain(o);
    if (unity_hop) {
        unity_hop = false;
        req.speed = 1.;
        bg->next(req);
    } else {
        req.speed = speed;
        bg->next(req);
    }
    req.speed = speed;
    analysed = false;
    double p0 = o.request[Bungee::OutputChunk::begin]->position;
    if (std::isnan(p0) || p0 < START)
        return;   // preroll and the grains before the handoff
    int n = o.frameCount;
    if (n > PEND_MAX - pend_n)
        n = PEND_MAX - pend_n;
    for (int i = 0; i < n; i++) {
        pend_l[pend_n + i] = o.data[i];
        pend_r[pend_n + i] = o.data[o.channelStride + i];
    }
    pend_n += n;
}

static void half_step()
{
    if (!analysed)
        analyse();
    else if (pend_n < SUB)
        synthesise();
}

static void start_launch(float ratio)
{
    req = {};
    req.position = START;
    req.speed = 1.;          // the preroll hop at speed 1: phases match the clip at START
    req.pitch = 1.;
    req.reset = true;
    req.resampleMode = resampleMode_autoOut;
    bg->preroll(req);
    speed = ratio;
    unity_hop = true;
    pend_n = 0;
    analysed = false;
    primed = false;
    launch = true;
    raw_pos = 0;
}

// One 256-frame sub-block (n <= SUB) into l/r: at most one half-grain of Bungee's work.
static void render(float *l, float *r, int n)
{
    int i;
    if (!primed) {
        // Priming: the raw Clip plays; the block that finishes priming plays raw too, or it would
        // also owe the next grain's analysis.
        half_step();
        primed = pend_n >= SUB;
        read_clip(raw_pos, n, l, r);
        raw_pos += n;
        return;
    }
    while (pend_n < n) {   // only if Bungee ever fell behind
        if (!analysed)
            analyse();
        synthesise();
    }
    for (i = 0; i < n; i++) {
        l[i] = pend_l[i];
        r[i] = pend_r[i];
    }
    if (launch) {
        // The handoff: the raw Clip carries on under Bungee's first XF frames. They are in phase
        // here (unity preroll hop), so a linear fade keeps the level.
        int k = n < XF ? n : XF;
        read_clip(raw_pos, k, xf_l, xf_r);
        for (i = 0; i < k; i++) {
            float a = (i + 0.5f) / XF;
            l[i] = a * l[i] + (1.f - a) * xf_l[i];
            r[i] = a * r[i] + (1.f - a) * xf_r[i];
        }
        launch = false;
    }
    pend_n -= n;
    for (i = 0; i < pend_n; i++) {
        pend_l[i] = pend_l[n + i];
        pend_r[i] = pend_r[n + i];
    }
    half_step();
}

void rubber_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    RbVoice *v = voice();
    uint32_t t0 = DWT_CYCCNT;
    if (!bg || !clip || !lane || !mix || !ctx)
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
    cur_sample = sample;
    cur_length = length;
    cur_stereo = stereo;

    float ratio = *(float *)(clip + 0xBB4);   // tempo / clip tempo (repitch.S hook_rate)
    if (!(ratio >= RB_MIN_RATIO))
        ratio = RB_MIN_RATIO;
    if (ratio > RB_MAX_RATIO)
        ratio = RB_MAX_RATIO;

    int reset = v->reset_req;
    v->reset_req = 0;
    unsigned char a0 = *(unsigned char *)(clip + 0x260), a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !v->prev_active)
        reset = 1;
    v->prev_active = (a0 || a1) ? 1 : 0;
    if (reset || !started)
        start_launch(ratio);
    started = true;
    speed = ratio;
    if (!unity_hop)
        req.speed = speed;

    static float mono_r[SUB];
    for (int at = 0; at < n; at += SUB) {
        int k = n - at < SUB ? n - at : SUB;
        render(dl + at, dr ? dr + at : mono_r, k);
    }

    FW_BUF_ENV((void *)(lane + 0x14c), scratch);
    FW_BUF_MIX((void *)mix, scratch);

    uint32_t cyc = DWT_CYCCNT - t0;
    acc_cyc += cyc;
    if (cyc > peak_cyc)
        peak_cyc = cyc;
    if (++n_blocks >= RB_BLOCKS_SEC) {
        v->avg_cyc = acc_cyc / n_blocks;
        v->peak_cyc = peak_cyc;
        // 480 MHz / 48 kHz = 10000 cycles per frame: % = cycles * 100 / (n * 10000).
        label_load(v->avg_cyc / ((uint32_t)n * 100u));
        acc_cyc = peak_cyc = n_blocks = 0;
    }
}
