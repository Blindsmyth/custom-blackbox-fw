#!/usr/bin/env python3
"""Does an optimised probe still sound like Rubber Band? Run two probe ELFs on the same input in Unicorn
(no per-instruction hooks, so it is quick) and compare their output sample by sample.

    .venv/bin/python tools/lab/rb/m7/compare.py ref.elf test.elf loop.wav mode ratio [blocks]

Prints the gain of test against ref, the SNR of test against ref (after best alignment within
+-64 samples), and writes both outputs as WAVs next to the test ELF for listening.
"""
import math
import struct
import sys
import wave

from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn import arm_const as A

CLIP, STOP = 0xC0000000, 0x08FFFF00


def load_elf(mu, path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    for i in range(phnum):
        p_type, p_off, p_vaddr, _pa, p_filesz, _m, _f, _a = struct.unpack_from("<IIIIIIII", data, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            mu.mem_write(p_vaddr, data[p_off:p_off + p_filesz])
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, _ = struct.unpack_from("<HHH", data, 46)
    secs = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    syms = {}
    for s in secs:
        if s[1] == 2:
            st = secs[s[6]]
            for k in range(s[5] // 16):
                nm, val, *_ = struct.unpack_from("<IIIBBH", data, s[4] + k * 16)
                n = data[st[4] + nm:].split(b"\0", 1)[0].decode()
                if n:
                    syms[n] = val
    return syms


def render(elf, L, R, frames, mode, ratio, blocks):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
    for base, size in ((0x08000000, 0x1000000), (0x24000000, 0x1000000), (CLIP, 0x1000000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)
    mu.mem_write(STOP, b"\x70\x47")
    syms = load_elf(mu, elf)
    sp = syms["__stack_top"] - 0x140
    mu.mem_write(CLIP, L)
    mu.mem_write(CLIP + 0x800000, R)

    def call(fn, *args, f=()):
        regs = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3]
        for i, a in enumerate(args):
            if i < 4:
                mu.reg_write(regs[i], a & 0xFFFFFFFF)
            else:
                mu.mem_write(sp + 4 * (i - 4), struct.pack("<I", a & 0xFFFFFFFF))
        for i, x in enumerate(f):
            mu.reg_write(A.UC_ARM_REG_S0 + i, struct.unpack("<I", struct.pack("<f", x))[0])
        mu.reg_write(A.UC_ARM_REG_SP, sp)
        mu.reg_write(A.UC_ARM_REG_LR, STOP | 1)
        mu.emu_start(fn | 1, STOP)
        return mu.reg_read(A.UC_ARM_REG_R0)

    call(syms["probe_boot"])
    delay = call(syms["probe_setup"], mode, CLIP, CLIP + 0x800000, frames, f=(ratio,))
    out_ptr = call(syms["probe_out"])
    out = []
    for _ in range(blocks):
        call(syms["probe_block"])
        out.extend(struct.unpack("<256f", bytes(mu.mem_read(out_ptr, 1024))))
    return out, delay


def write_wav(path, x):
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(48000)
    w.writeframes(struct.pack(f"<{len(x)}h", *(max(-32768, min(32767, int(v * 32767))) for v in x)))
    w.close()


def main():
    ref_elf, test_elf, wav = sys.argv[1:4]
    mode, ratio = int(sys.argv[4]), float(sys.argv[5])
    blocks = int(sys.argv[6]) if len(sys.argv) > 6 else 120
    w = wave.open(wav)
    n, ch = w.getnframes(), w.getnchannels()
    pcm = struct.unpack(f"<{n * ch}h", w.readframes(n))
    L = struct.pack(f"<{n}f", *(pcm[i * ch] / 32768 for i in range(n)))
    R = struct.pack(f"<{n}f", *(pcm[i * ch + ch - 1] / 32768 for i in range(n)))
    a, da = render(ref_elf, L, R, n, mode, ratio, blocks)
    b, db = render(test_elf, L, R, n, mode, ratio, blocks)
    skip = 8 * 256  # settle
    a, b = a[skip:], b[skip:]
    ea = sum(x * x for x in a) or 1e-20
    eb = sum(x * x for x in b)
    best = None
    for lag in range(-64, 65):
        err = 0.0
        for i in range(64, len(a) - 64):
            d = a[i] - b[i + lag]
            err += d * d
        if best is None or err < best[0]:
            best = (err, lag)
    snr = 10 * math.log10(ea / max(best[0], 1e-20))
    print(f"mode {mode} ratio {ratio}: start delay ref {da} test {db}; gain test/ref {math.sqrt(eb / ea):.3f}; "
          f"SNR {snr:.1f} dB at lag {best[1]}")
    write_wav(test_elf.replace(".elf", f"_m{mode}_r{ratio}.wav"), b)
    write_wav(ref_elf.replace(".elf", f"_m{mode}_r{ratio}.wav"), a)


if __name__ == "__main__":
    main()
