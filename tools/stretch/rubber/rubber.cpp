// Warp "Rubber": Rubber Band (R2, the Mixxx "faster" engine) rendering one Clip, in flash bank 2.
//
// Bank 1 calls rubber_boot() once per power-up, after b2pair.c has checked the payload, and
// rubber_mix() from the per-lane render hook (repitch.S hook_lane) for the one Clip that owns
// the voice. Everything this file allocates comes from the SDRAM reserve (rt.c), and its .data
// and .bss live in SRAM1 (b2.ld), so nothing here touches stock's memory.
//
// Real-time mode does its FFT work per hop, so the cost per 256-frame block is even at tempo
// ratios up to about 1.25 and comes in bursts above that (tools/lab/rb/m7 measurements); the
// ratio is clamped to RB_MIN_RATIO..RB_MAX_RATIO. A launch primes the stretcher over several
// blocks (silence) instead of in one, so a retrigger never overruns a block.
#include "rubber.h"

#include "rubberband/RubberBandStretcher.h"

#include <cstdint>

using RubberBand::RubberBandStretcher;

#define RB_MIN_RATIO 0.5f
#define RB_MAX_RATIO 1.25f
#define RB_FEED      512     /* frames per process() call */
#define RB_BLOCKS_SEC 188    /* 48000 / 256: stats period */

extern "C" {
extern uint32_t __b2_data_load[], __b2_data_start[], __b2_data_end[];
extern uint32_t __b2_bss_start[], __b2_bss_end[];
extern void (*__b2_init_array_start[])(void);
extern void (*__b2_init_array_end[])(void);
void rubber_boot(void);
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

// Stock 3.1.9 functions, as used by Warp Capi (warp-capi branch tools/stretch/capi_fw.c).
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

static RubberBandStretcher *rb;
static float feed_l[RB_FEED], feed_r[RB_FEED], drop_buf[2][256];
static int32_t read_pos;
static float cur_ratio;
static int32_t pad_left, drop_left;
static uint32_t acc_cyc, peak_cyc, n_blocks;

static RbVoice *voice() { return (RbVoice *)RB_VOICE; }

static void set_label(const char *s)
{
    char *d = voice()->label;
    int i = 0;
    for (; s[i] && i < 15; i++)
        d[i] = s[i];
    d[i] = 0;
}

// "Rubber 54%": rubber_mix's own share of the 256-frame budget, last second.
static void label_load(uint32_t pct)
{
    char s[16] = "Rubber ";
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

void rubber_boot(void)
{
    // Bank 2's .data from flash, .bss zeroed, then its C++ constructors (newlib, libstdc++).
    for (uint32_t *s = __b2_data_load, *d = __b2_data_start; d < __b2_data_end;)
        *d++ = *s++;
    for (uint32_t *d = __b2_bss_start; d < __b2_bss_end;)
        *d++ = 0;
    for (void (**f)(void) = __b2_init_array_start; f < __b2_init_array_end; f++)
        (*f)();

    DEMCR |= 1u << 24;   // TRCENA: DWT for the load figure
    DWT_CTRL |= 1u;      // CYCCNTENA

    // Built for the slowest tempo so no buffer has to grow while playing.
    rb = new RubberBandStretcher(48000, 2,
                                 RubberBandStretcher::OptionProcessRealTime |
                                 RubberBandStretcher::OptionThreadingNever,
                                 1.0 / RB_MIN_RATIO, 1.0);
    rb->setTimeRatio(1.0);
    cur_ratio = 1.f;
    set_label("Rubber");
}

static void feed(void *sample, int length, int stereo, int frames)
{
    while (frames > 0) {
        int k = frames < RB_FEED ? frames : RB_FEED, got = 0;
        while (got < k) {
            int n = length - read_pos;
            if (n > k - got)
                n = k - got;
            FW_PCM_READ(sample, feed_l + got, n, read_pos, 0);
            if (stereo)
                FW_PCM_READ(sample, feed_r + got, n, read_pos, 1);
            else
                for (int i = 0; i < n; i++)
                    feed_r[got + i] = feed_l[got + i];
            got += n;
            read_pos += n;
            if (read_pos >= length)
                read_pos = 0;
        }
        const float *p[2] = { feed_l, feed_r };
        rb->process(p, k, false);
        frames -= k;
    }
}

static void feed_zeros(int frames)
{
    for (int i = 0; i < RB_FEED; i++)
        feed_l[i] = feed_r[i] = 0.f;
    while (frames > 0) {
        int k = frames < RB_FEED ? frames : RB_FEED;
        const float *p[2] = { feed_l, feed_r };
        rb->process(p, k, false);
        frames -= k;
    }
}

static int required()
{
    size_t r = rb->getSamplesRequired();
    if (r == 0)
        r = 256;
    return (int)r;
}

void rubber_mix(unsigned clip, unsigned lane, unsigned mix, unsigned ctx)
{
    RbVoice *v = voice();
    uint32_t t0 = DWT_CYCCNT;
    if (!rb || !clip || !lane || !mix || !ctx)
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

    int reset = v->reset_req;
    v->reset_req = 0;
    unsigned char a0 = *(unsigned char *)(clip + 0x260), a1 = *(unsigned char *)(clip + 0x670);
    if ((a0 || a1) && !v->prev_active)
        reset = 1;
    v->prev_active = (a0 || a1) ? 1 : 0;
    if (read_pos >= length)
        read_pos = 0;

    if (reset) {
        rb->reset();
        read_pos = 0;
        pad_left = (int)rb->getPreferredStartPad();
        drop_left = (int)rb->getStartDelay();
    }
    if (ratio != cur_ratio) {
        rb->setTimeRatio(1.0 / ratio);
        cur_ratio = ratio;
    }

    if (pad_left > 0) {
        // Priming, one block's worth at a time: the block stays silent.
        int k = pad_left < 256 ? pad_left : 256;
        feed_zeros(k);
        pad_left -= k;
    } else if (drop_left > 0) {
        // Drop the start delay, at most one hop per block (silent).
        if (rb->available() < 1)
            feed(sample, length, stereo, required());
        int k = rb->available();
        if (k > drop_left)
            k = drop_left;
        if (k > 256)
            k = 256;
        if (k > 0) {
            float *o[2] = { drop_buf[0], drop_buf[1] };
            rb->retrieve(o, (size_t)k);
            drop_left -= k;
        }
    } else {
        int done = 0;
        while (done < n) {
            int avail = rb->available();
            if (avail < 1) {
                feed(sample, length, stereo, required());
                continue;
            }
            int k = avail < n - done ? avail : n - done;
            float *o[2] = { dl + done, (dr ? dr : dl) + done };
            rb->retrieve(o, (size_t)k);
            done += k;
        }
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
