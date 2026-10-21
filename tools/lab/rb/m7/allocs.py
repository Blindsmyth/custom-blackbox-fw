#!/usr/bin/env python3
"""Who allocates Rubber Band's heap? Runs probe_boot + probe_setup and logs every _sbrk growth with the
function that asked (walks LR back to the nearest symbol), biggest first.

    .venv/bin/python tools/lab/rb/m7/allocs.py probe.elf loop.wav mode ratio
"""
import bisect
import struct
import sys
import wave

from unicorn import UC_HOOK_CODE
from unicorn import arm_const as A

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import compare  # noqa: E402  (reuses its loader)

elf, wav, mode, ratio = sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4])
w = wave.open(wav)
n, ch = w.getnframes(), w.getnchannels()
pcm = struct.unpack(f"<{n * ch}h", w.readframes(n))
L = struct.pack(f"<{n}f", *(pcm[i * ch] / 32768 for i in range(n)))

from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc  # noqa: E402
mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
for base, size in ((0x08000000, 0x1000000), (0x24000000, 0x1000000), (0xC0000000, 0x1000000), (0xE0000000, 0x100000)):
    mu.mem_map(base, size)
mu.mem_write(compare.STOP, b"\x70\x47")
syms = compare.load_elf(mu, elf)
mu.mem_write(0xC0000000, L)
mu.mem_write(0xC0800000, L)
funcs = sorted((v & ~1, k) for k, v in syms.items() if 0x08000000 <= v < 0x09000000)
addrs = [a for a, _ in funcs]

# Allocation call chain: stack-walk is unreliable, so record the caller of malloc/_malloc_r and of operator new.
want = {syms[k] & ~1: k for k in ("_sbrk", "malloc", "_malloc_r", "_Znwj", "_Znaj") if k in syms}
log = []
stack = []


def name(a):
    i = bisect.bisect_right(addrs, a & ~1) - 1
    return funcs[i][1] if i >= 0 else hex(a)


def hook(uc, addr, size, _):
    k = want.get(addr)
    if k == "_sbrk":
        log.append((uc.reg_read(A.UC_ARM_REG_R0), list(stack[-6:])))
    elif k in ("_Znwj", "_Znaj", "malloc"):
        stack.append(name(uc.reg_read(A.UC_ARM_REG_LR)))


for a in want:
    mu.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
sp = syms["__stack_top"] - 0x140


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
    mu.reg_write(A.UC_ARM_REG_LR, compare.STOP | 1)
    mu.emu_start(fn | 1, compare.STOP)
    return mu.reg_read(A.UC_ARM_REG_R0)


call(syms["probe_boot"])
call(syms["probe_setup"], mode, 0xC0000000, 0xC0800000, n, f=(ratio,))
total = sum(x for x, _ in log if x > 0)
print(f"heap grown {total / 1024:.0f} KB in {len(log)} sbrk calls")
agg = {}
for x, who in log:
    key = " < ".join(reversed(who)) or "?"
    agg[key] = agg.get(key, 0) + x
for k, v in sorted(agg.items(), key=lambda t: -t[1])[:12]:
    print(f"  {v / 1024:8.0f} KB  {k}")
