/* Offline Warp prototype: stretch a real WAV the same way the cave does.
 *
 *   clang -O2 -o tools/stretch/proto tools/stretch/proto.c \
 *       tools/stretch/smooth.c tools/stretch/snap.c tools/stretch/vari.c -lm
 *   tools/stretch/proto --mode smooth --in loop.wav --ratio 1.25 --out out/smooth.wav
 *   tools/stretch/proto --mode snap --in loop.wav --slices 0,12000,24000 --out out/snap.wav
 *   tools/stretch/proto --mode vari --in loop.wav --ratio 1.25 --out out/vari.wav
 *   tools/stretch/proto --mode grain --in loop.wav --ratio 1.25 --out out/grain.wav
 *   tools/stretch/proto --mode naive --in loop.wav --ratio 1.25 --out out/naive.wav
 *
 * Uses 1024-sample blocks like the Blackbox DMA half so join bugs show up here.
 */
#include "smooth.h"
#include "snap.h"
#include "vari.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float *l;
    float *r;
    int n;
    int ch;
    int sr;
} Wav;

static void die(const char *msg)
{
    fprintf(stderr, "proto: %s\n", msg);
    exit(1);
}

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static Wav read_wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[12];
    Wav w = {0};
    unsigned size, pos;
    unsigned char *file;
    unsigned fmt_ofs = 0, data_ofs = 0, data_sz = 0;
    unsigned fmt_sz, audio_fmt, ch, sr, bps;
    int i, frames;
    if (!f)
        die("cannot open input");
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
        die("not a RIFF/WAVE file");
    size = rd32(hdr + 4);
    if (size < 36 || size > 64u * 1024u * 1024u)
        die("WAV too large or corrupt");
    file = malloc(8 + size);
    if (!file)
        die("oom");
    memcpy(file, hdr, 12);
    if (fread(file + 12, 1, size - 4, f) != size - 4)
        die("short read");
    fclose(f);
    pos = 12;
    while (pos + 8 <= 8 + size) {
        unsigned id = rd32(file + pos);
        unsigned sz = rd32(file + pos + 4);
        unsigned next = pos + 8 + ((sz + 1u) & ~1u);
        if (id == 0x20746d66u) { /* 'fmt ' */
            fmt_ofs = pos + 8;
            fmt_sz = sz;
        } else if (id == 0x61746164u) { /* 'data' */
            data_ofs = pos + 8;
            data_sz = sz;
        }
        if (next <= pos)
            break;
        pos = next;
    }
    if (!fmt_ofs || !data_ofs || fmt_sz < 16)
        die("WAV missing fmt/data");
    audio_fmt = rd16(file + fmt_ofs);
    ch = rd16(file + fmt_ofs + 2);
    sr = rd32(file + fmt_ofs + 4);
    bps = rd16(file + fmt_ofs + 14);
    if (audio_fmt != 1 || (ch != 1 && ch != 2) || bps != 16)
        die("need 16-bit PCM mono or stereo");
    frames = (int)(data_sz / (ch * 2u));
    if (frames < SMOOTH_WIN)
        die("input shorter than one Smooth window");
    w.l = calloc((size_t)frames, sizeof(float));
    w.r = calloc((size_t)frames, sizeof(float));
    if (!w.l || !w.r)
        die("oom");
    for (i = 0; i < frames; i++) {
        const unsigned char *p = file + data_ofs + (unsigned)i * ch * 2u;
        short s = (short)rd16(p);
        w.l[i] = (float)s / 32768.f;
        if (ch == 2) {
            short t = (short)rd16(p + 2);
            w.r[i] = (float)t / 32768.f;
        } else {
            w.r[i] = w.l[i];
        }
    }
    free(file);
    w.n = frames;
    w.ch = (int)ch;
    w.sr = (int)sr;
    return w;
}

static void write_wav(const char *path, const float *l, const float *r, int n, int sr, int stereo)
{
    FILE *f = fopen(path, "wb");
    unsigned char h[44];
    int i;
    unsigned data = (unsigned)n * (stereo ? 4u : 2u);
    unsigned br = (unsigned)sr * (stereo ? 4u : 2u);
    if (!f)
        die("cannot open output");
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4);
    {
        unsigned sz = 36 + data;
        memcpy(h + 4, &sz, 4);
    }
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16;
    h[20] = 1;
    h[22] = (unsigned char)(stereo ? 2 : 1);
    memcpy(h + 24, &sr, 4);
    memcpy(h + 28, &br, 4);
    h[32] = (unsigned char)(stereo ? 4 : 2);
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    for (i = 0; i < n; i++) {
        float a = l[i], b = r ? r[i] : l[i];
        short sa, sb;
        if (a > 1.f) a = 1.f;
        if (a < -1.f) a = -1.f;
        if (b > 1.f) b = 1.f;
        if (b < -1.f) b = -1.f;
        sa = (short)(a * 32767.f);
        fwrite(&sa, 2, 1, f);
        if (stereo) {
            sb = (short)(b * 32767.f);
            fwrite(&sb, 2, 1, f);
        }
    }
    fclose(f);
}

static int read_chs(void *sample, float *dest, int count, int pos, int chan)
{
    const float **chs = (const float **)sample;
    const float *src = chs[chan ? 1 : 0];
    int i;
    for (i = 0; i < count; i++)
        dest[i] = src[pos + i];
    return 1;
}

/* Stock Clip Stretch ≈ win 256 / hop 128. Warp Grain ≈ win 2048 / hop 1024. */
static void ola_grain(const float *src, int length, float *dst, int n, float ratio,
                      int win, int hop)
{
    int pos = 0, out = 0, have = 0, emitted = hop;
    float *acc;
    if (win < 2 || hop < 1 || hop * 2 > win)
        die("ola_grain: need hop*2 <= win");
    acc = calloc((size_t)win, sizeof(float));
    if (!acc)
        die("oom");
    while (out < n) {
        int i, take;
        if (!have || emitted >= hop) {
            int ip;
            if (have) {
                for (i = 0; i < hop; i++)
                    acc[i] = acc[i + hop];
                for (i = hop; i < win; i++)
                    acc[i] = 0.f;
            }
            ip = pos < 0 ? 0 : pos;
            for (i = 0; i < win; i++) {
                float t = (float)i / (float)win;
                float w = 0.5f * (1.f - cosf(6.28318530718f * t));
                int p = ip + i;
                while (p >= length)
                    p -= length;
                acc[i] += src[p] * w;
            }
            pos += (int)((float)hop * ratio);
            while (length > 0 && pos >= length)
                pos -= length;
            have = 1;
            emitted = 0;
        }
        take = hop - emitted;
        if (take > n - out)
            take = n - out;
        for (i = 0; i < take; i++)
            dst[out + i] += acc[emitted + i];
        emitted += take;
        out += take;
    }
    free(acc);
}

static float crackle_score(const float *x, int n)
{
    /* Count large sample-to-sample jumps; hop-edge clicks show up here. */
    int i, hits = 0;
    for (i = 1; i < n; i++) {
        float d = x[i] - x[i - 1];
        if (d < 0.f)
            d = -d;
        if (d > 0.25f)
            hits++;
    }
    return (float)hits * 1000.f / (float)n;
}

static int parse_slices(const char *s, uint32_t *out, int max)
{
    int n = 0;
    const char *p = s;
    if (!s || !*s)
        return 0;
    while (*p && n < max) {
        char *end = 0;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p)
            die("bad --slices list (need comma-separated sample indices)");
        out[n++] = (uint32_t)v;
        if (*end == 0)
            break;
        if (*end != ',')
            die("bad --slices list");
        p = end + 1;
    }
    return n;
}

static void dump_marks(const char *path, const uint32_t *marks, int n)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f)
        die("cannot open --marks output");
    for (i = 0; i < n; i++)
        fprintf(f, "%u\n", marks[i]);
    fclose(f);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: proto --in file.wav --out file.wav [--mode smooth|snap|vari|grain|naive]\n"
            "             [--ratio 1.25] [--mono] [--slices p0,p1,...] [--marks file.txt]\n"
            "  16-bit PCM wav in. Renders in 1024-sample blocks (device DMA half).\n"
            "  --slices is used by --mode snap (Sync-to-Slice style markers).\n"
            "  --mode vari builds an epoch/onset map; --marks dumps it.\n"
            "  --mode grain is Clip_StartGrain-like OLA at win 2048 / hop 1024;\n"
            "  --mode naive is stock Stretch-like OLA at win 256 / hop 128.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *in_path = 0, *out_path = 0, *mode = "smooth", *slices_arg = 0;
    const char *marks_path = 0;
    float ratio = 1.25f;
    int force_mono = 0, i, out_n, block = SMOOTH_HOP;
    Wav in;
    float *ol, *or_;
    const float *chs[2];
    uint32_t slices[256];
    int n_slices = 0, n_marks_report = 0;
    const char *tag;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--in") && i + 1 < argc)
            in_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "--ratio") && i + 1 < argc)
            ratio = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc)
            mode = argv[++i];
        else if (!strcmp(argv[i], "--slices") && i + 1 < argc)
            slices_arg = argv[++i];
        else if (!strcmp(argv[i], "--marks") && i + 1 < argc)
            marks_path = argv[++i];
        else if (!strcmp(argv[i], "--naive"))
            mode = "naive";
        else if (!strcmp(argv[i], "--mono"))
            force_mono = 1;
        else
            usage();
    }
    if (!in_path || !out_path)
        usage();
    if (!(ratio > 0.25f) || ratio > 4.f)
        die("ratio must be in 0.25 .. 4");
    if (strcmp(mode, "smooth") && strcmp(mode, "snap") &&
        strcmp(mode, "vari") && strcmp(mode, "grain") && strcmp(mode, "naive"))
        die("--mode must be smooth, snap, vari, grain, or naive");
    if (slices_arg)
        n_slices = parse_slices(slices_arg, slices, 256);

    in = read_wav(in_path);
    if (in.sr != 48000)
        fprintf(stderr, "proto: warning: sample rate %d (device is 48000); still processing as-is\n", in.sr);

    out_n = (int)((float)in.n / ratio);
    if (out_n < block)
        die("output too short");
    ol = calloc((size_t)out_n, sizeof(float));
    or_ = calloc((size_t)out_n, sizeof(float));
    if (!ol || !or_)
        die("oom");

    chs[0] = in.l;
    chs[1] = in.r;
    tag = mode;
    if (!strcmp(mode, "naive")) {
        ola_grain(in.l, in.n, ol, out_n, ratio, 256, 128);
        memcpy(or_, ol, (size_t)out_n * sizeof(float));
    } else if (!strcmp(mode, "grain")) {
        ola_grain(in.l, in.n, ol, out_n, ratio, 2048, 1024);
        memcpy(or_, ol, (size_t)out_n * sizeof(float));
    } else if (!strcmp(mode, "snap")) {
        SnapVoice *voice = calloc(1, sizeof *voice);
        int done = 0, stereo = (!force_mono && in.ch == 2);
        if (!voice)
            die("oom");
        snap_init(voice);
        while (done < out_n) {
            int n = out_n - done;
            if (n > block)
                n = block;
            snap_process(voice, (void *)chs, in.n, stereo, read_chs,
                         n_slices > 0 ? slices : 0, n_slices,
                         ol + done, or_ + done, n, ratio, done == 0);
            done += n;
        }
        free(voice);
        if (n_slices == 0)
            fprintf(stderr, "proto: snap with no --slices (fixed-hop XF only)\n");
    } else if (!strcmp(mode, "vari")) {
        VariVoice *voice = calloc(1, sizeof *voice);
        uint32_t *marks = calloc(VARI_MAX_MARKS, sizeof(uint32_t));
        int n_marks, done = 0, stereo = (!force_mono && in.ch == 2);
        if (!voice || !marks)
            die("oom");
        {
            float *ascratch = calloc(VARI_SCRATCH_N, sizeof(float));
            if (!ascratch)
                die("oom");
            n_marks = vari_analyze((void *)chs, in.n, stereo, read_chs,
                                   marks, VARI_MAX_MARKS, ascratch, VARI_SCRATCH_N);
            free(ascratch);
        }
        if (n_marks < 2)
            die("vari_analyze produced too few marks");
        n_marks_report = n_marks;
        if (marks_path)
            dump_marks(marks_path, marks, n_marks);
        fprintf(stderr, "proto: vari marks=%d\n", n_marks);
        vari_init(voice);
        while (done < out_n) {
            int n = out_n - done;
            if (n > block)
                n = block;
            vari_process(voice, (void *)chs, in.n, stereo, read_chs,
                         marks, n_marks,
                         ol + done, or_ + done, n, ratio, done == 0);
            done += n;
        }
        free(voice);
        free(marks);
    } else {
        SmoothVoice *voice = calloc(1, sizeof *voice);
        int done = 0, stereo = (!force_mono && in.ch == 2);
        if (!voice)
            die("oom");
        smooth_init(voice);
        while (done < out_n) {
            int n = out_n - done;
            if (n > block)
                n = block;
            smooth_process(voice, (void *)chs, in.n, stereo, read_chs,
                           ol + done, or_ + done, n, ratio, done == 0);
            done += n;
        }
        free(voice);
    }

    {
        float peak = 0.f, cL = crackle_score(ol, out_n), cR = crackle_score(or_, out_n);
        for (i = 0; i < out_n; i++) {
            float a = ol[i] < 0.f ? -ol[i] : ol[i];
            float b = or_[i] < 0.f ? -or_[i] : or_[i];
            if (a > peak) peak = a;
            if (b > peak) peak = b;
        }
        printf("%s  in=%d  out=%d  sr=%d  ratio=%.3f  slices=%d  marks=%d  peak=%.3f  crackle/k=%.2f/%.2f\n",
               tag, in.n, out_n, in.sr, ratio, n_slices, n_marks_report, peak, cL, cR);
        printf("  (crackle/k = large |Δ| jumps per 1000 samples; hop clicks raise this)\n");
    }

    write_wav(out_path, ol, or_, out_n, in.sr, !force_mono && in.ch == 2);
    printf("wrote %s\n", out_path);
    free(in.l);
    free(in.r);
    free(ol);
    free(or_);
    return 0;
}
