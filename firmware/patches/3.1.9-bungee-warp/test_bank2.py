#!/usr/bin/env python3
"""Run the packed bank-2 payload (out/BBEXTRA.BIN) in Unicorn the way bank 1 drives it.

    ../../../tools/lab/rb/m7/.venv/bin/python test_bank2.py [ratio ...]

Maps the payload at 0x08100000, SRAM4 (the voice slot holds the leaf's .data/.bss), a 2 MB
SDRAM arena and the clip. bg_read is a host hook. Checks, on a drum loop and on sines:
a launch plays the clip itself, sample for sample, for the 8 priming blocks; the handoff to
Bungee has no level dip (> -1.5 dB) or step; no block costs more than ~half a grain;
and bg_init over a clobbered slot (another Warp mode took it) relaunches identically.
BBEXTRA=path tests another build of the payload.
"""
import math
import os
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn import arm_const as A

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
M7 = ROOT / "tools" / "lab" / "rb" / "m7"
sys.path.insert(0, str(M7))

MD = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
SYMS = {}
for line in (HERE / "bg" / "bank2_syms.h").read_text().splitlines():
    p = line.split()
    if len(p) == 3 and p[0] == "#define" and p[1].endswith("_ADDR"):
        SYMS[p[1]] = int(p[2].rstrip("u"), 16)

ARENA, ARENA_LEN = 0xC0000000, 2 << 20
CLIP = 0xC0800000
READ = 0x08FF0000
STOP = 0x08FFFF00
SP = 0x24080000
OUT = 0x24100000
N = 256
BLOCKS = 48


def run(ratio, L, R, frames, clobber_at=None):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
    for base, size in ((0x08000000, 0x1000000), (0x24000000, 0x200000), (0x38000000, 0x10000),
                       (0xC0000000, 0x1000000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)
    mu.mem_write(0x08100000, Path(os.environ.get("BBEXTRA", HERE / "out" / "BBEXTRA.BIN")).read_bytes())
    mu.mem_write(0x38000000, b"\xa5" * 0x10000)  # SRAM4 is random at power-on
    mu.mem_write(STOP, b"\x70\x47")
    mu.mem_write(READ, b"\x70\x47")
    mu.mem_write(CLIP, L)
    mu.mem_write(CLIP + 0x400000, R)
    insns = [0]
    counts = {}

    def on_block(uc, addr, size, _):
        k = counts.get((addr, size))
        if k is None:
            k = counts[(addr, size)] = len(list(MD.disasm(bytes(uc.mem_read(addr, size)), addr)))
        insns[0] += k

    def on_read(uc, addr, size, _):
        pos = uc.reg_read(A.UC_ARM_REG_R1)
        pos -= (pos & 0x80000000) << 1
        n = uc.reg_read(A.UC_ARM_REG_R2)
        dl = uc.reg_read(A.UC_ARM_REG_R3)
        dr = struct.unpack("<I", bytes(uc.mem_read(uc.reg_read(A.UC_ARM_REG_SP), 4)))[0]
        for dest, base in ((dl, CLIP), (dr, CLIP + 0x400000)):
            buf = bytearray()
            for i in range(n):
                p = pos + i
                buf += bytes(uc.mem_read(base + 4 * (p % frames), 4))  # wraps like the probe
            uc.mem_write(dest, bytes(buf))

    mu.hook_add(UC_HOOK_BLOCK, on_block)
    mu.hook_add(UC_HOOK_CODE, on_read, begin=READ, end=READ + 1)

    def call(fn, *args, f=()):
        regs = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3]
        sp = SP - 0x40
        for i, a in enumerate(args):
            if i < 4:
                mu.reg_write(regs[i], a & 0xFFFFFFFF)
            else:
                mu.mem_write(sp + 4 * (i - 4), struct.pack("<I", a & 0xFFFFFFFF))
        for i, x in enumerate(f):
            mu.reg_write(A.UC_ARM_REG_S0 + i, struct.unpack("<I", struct.pack("<f", x))[0])
        mu.reg_write(A.UC_ARM_REG_SP, sp)
        mu.reg_write(A.UC_ARM_REG_LR, STOP | 1)
        insns[0] = 0
        try:
            mu.emu_start(fn | 1, STOP)
        except Exception as err:
            raise SystemExit(f"fault at {mu.reg_read(A.UC_ARM_REG_PC):#x}: {err}")
        return mu.reg_read(A.UC_ARM_REG_R0), insns[0]

    def init():
        rc, ni = call(SYMS["BG_INIT_ADDR"], ARENA, ARENA_LEN)
        if rc != 0:
            raise SystemExit(f"bg_init returned {rc}")
        call(SYMS["BG_RESTART_ADDR"], f=(ratio,))
        return ni

    setup = init()
    out, cost = [], []
    for b in range(BLOCKS):
        if b == clobber_at:
            mu.mem_write(0x38007000, b"\x5a" * 0x7800)  # Smooth/Capi took the slot meanwhile
            init()
        _, ni = call(SYMS["BG_MIX_ADDR"], READ | 1, 0, OUT, OUT + 4 * N, N, f=(ratio,))
        out.extend(struct.unpack(f"<{N}f", bytes(mu.mem_read(OUT, 4 * N))))
        cost.append(ni)
    return out, cost, setup


def rms_db(x):
    return 10 * math.log10(sum(v * v for v in x) / len(x) + 1e-20)


def handoff(out, src, T, W=256):
    """Level through the handoff: the lowest W-frame window in [T-256, T+384] relative to
    the windows around it (dB), minus the same measure on the unprocessed clip (so a
    signal's own ripple is not a dip; 0 = no dip). And the largest sample step in the
    crossfade relative to the largest elsewhere."""
    def dip(x):
        win = lambda a: [rms_db(x[k:k + W]) for k in range(a, a + 512, 32)]
        ref = sum(win(T - 1280) + win(T + 640)) / 32
        return min(rms_db(x[k:k + W]) for k in range(T - 256 - W // 2, T + 384 - W // 2, 16)) - ref
    step = lambda a, b: max(abs(out[k + 1] - out[k]) for k in range(a, b))
    return dip(out) - dip(src[:len(out)]), step(T - 8, T + 136) / step(T + 600, T + 3000)


def clip(kind, n=48000):
    if kind == "loop":
        import wave
        w = wave.open(str(M7 / "test_loop.wav"))
        n, ch = w.getnframes(), w.getnchannels()
        pcm = struct.unpack(f"<{n * ch}h", w.readframes(n))
        l = [pcm[i * ch] / 32768 for i in range(n)]
        r = [pcm[i * ch + ch - 1] / 32768 for i in range(n)]
    else:
        fs = [float(f) for f in kind.split("+")]
        l = [sum(0.4 / len(fs) * math.sin(2 * math.pi * f * i / 48000 + k) for k, f in enumerate(fs))
             for i in range(n)]
        r = l
    return l, r, n


def main():
    ratios = [float(r) for r in sys.argv[1:]] or [0.8, 1.25]
    ok = True
    T = 8 * N
    for kind in ("loop", "220", "1000", "220+277+330"):
        l, r, n = clip(kind)
        L, R = struct.pack(f"<{n}f", *l), struct.pack(f"<{n}f", *r)
        for ratio in ratios:
            out, cost, setup = run(ratio, L, R, n)
            raw_err = max(abs(out[i] - struct.unpack("<f", struct.pack("<f", r[i % n]))[0]) for i in range(T))
            dip, jump = handoff(out, l, T)
            bad = any(math.isnan(x) for x in out)
            line = (f"{kind:12} ratio {ratio}: raw launch err {raw_err:.1e}, handoff dip {dip:+.1f} dB, "
                    f"step x{jump:.2f}, insns/block peak {max(cost) / 1e6:.2f}M{', NaN!' if bad else ''}")
            print(line, flush=True)
            ok &= raw_err == 0 and not bad and max(cost) < 1.4e6 and jump < 1.15
            ok &= dip > -1.5
            if kind == "loop" and ratio == ratios[0]:
                out2, _, _ = run(ratio, L, R, n, clobber_at=20)
                same = max(abs(x - y) for x, y in zip(out2[20 * N:], out))
                print(f"  re-init over a clobbered slot at block 20: max diff vs a fresh launch {same:.2e}")
                ok &= same < 1e-6
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
