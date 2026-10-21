#!/usr/bin/env python3
"""Count Rubber Band's cost on a Cortex-M7 (Unicorn), one stereo stream, 256-frame blocks.

    tools/lab/rb/m7/build.sh
    .venv/bin/python tools/lab/rb/m7/measure.py tools/lab/rb/m7/probe.elf loop.wav [mode ...]

Reports instructions per block and an estimated cycle count. The estimate weights the
FPU ops the M7 does not pipeline (ARM Cortex-M7 TRM: VDIV/VSQRT.F32 14 cycles, .F64 ~30;
double-precision arithmetic 2-3) and counts everything else as 1 cycle. It ignores dual
issue (cheaper) and cache misses / SDRAM waits (dearer). Budget: 480 MHz x 5.33 ms =
2.56M cycles per 256-frame block.
"""
import struct
import sys
import wave

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn import arm_const as A

ELF, WAV = sys.argv[1], sys.argv[2]
MODES = [int(m) for m in sys.argv[3:]] or [0, 1, 2]
import os
BLOCKS = int(os.environ.get("BLOCKS", "120"))
RATIOS = [float(r) for r in os.environ.get("RATIOS", "0.8,0.6,1.25").split(",")]
PROFILE = os.environ.get("PROFILE")
BUDGET = 480e6 * 256 / 48000
CLIP = 0xC0000000
STOP = 0x08FFFF00

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)


def load_elf(mu, path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    for i in range(phnum):
        p_type, p_off, p_vaddr, _p_paddr, p_filesz, _memsz, _f, _a = struct.unpack_from(
            "<IIIIIIII", data, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            mu.mem_write(p_vaddr, data[p_off:p_off + p_filesz])
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    secs = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    syms = {}
    for s in secs:
        if s[1] == 2:  # SHT_SYMTAB
            strtab = secs[s[6]]
            for k in range(s[5] // 16):
                name, value, size, _info, _other, _shndx = struct.unpack_from("<IIIBBH", data, s[4] + k * 16)
                n = data[strtab[4] + name:].split(b"\0", 1)[0].decode()
                if n:
                    syms[n] = value
                    SIZES[n] = (value & ~1, size)
    return syms


def weight(insn):
    m = insn.mnemonic
    if m.startswith(("vdiv", "vsqrt")):
        return 30 if ".f64" in m else 14
    if ".f64" in m:
        return 3 if m.startswith(("vfma", "vfms", "vmla", "vmls", "vfnma", "vfnms")) else 2
    if m.startswith(("ldm", "stm", "push", "pop", "vldm", "vstm", "vpush", "vpop")):
        return max(1, insn.op_str.count(",") + 1)
    return 1


SIZES = {}
# Soft-float double helpers (FPU=sp build) and what the M7's DP FPU does instead, in cycles.
HW_COST = {
    "__aeabi_dadd": 2, "__adddf3": 2, "__aeabi_dsub": 2, "__subdf3": 2, "__aeabi_drsub": 2,
    "__aeabi_dmul": 2, "__muldf3": 2, "__aeabi_ddiv": 30, "__divdf3": 30,
    "__aeabi_dcmpeq": 2, "__aeabi_dcmplt": 2, "__aeabi_dcmple": 2, "__aeabi_dcmpge": 2,
    "__aeabi_dcmpgt": 2, "__aeabi_dcmpun": 2, "__aeabi_cdcmpeq": 2, "__aeabi_cdcmple": 2,
    "__aeabi_cdrcmple": 2, "__eqdf2": 2, "__nedf2": 2, "__ltdf2": 2, "__gtdf2": 2, "__ledf2": 2,
    "__gedf2": 2, "__cmpdf2": 2, "__unorddf2": 2,
    "__aeabi_f2d": 1, "__extendsfdf2": 1, "__aeabi_d2f": 1, "__truncdfsf2": 1,
    "__aeabi_i2d": 1, "__floatsidf": 1, "__aeabi_ui2d": 1, "__floatunsidf": 1,
    "__aeabi_l2d": 4, "__floatdidf": 4, "__aeabi_ul2d": 4, "__floatundidf": 4,
    "__aeabi_d2iz": 1, "__fixdfsi": 1, "__aeabi_d2uiz": 1, "__fixunsdfsi": 1,
    "__aeabi_d2lz": 4, "__fixdfdi": 4, "__aeabi_d2ulz": 4, "__fixunsdfdi": 4,
    "sqrt": 4, "__ieee754_sqrt": 30, "fabs": 1,
}
helper_ranges = []   # (start, end, cost_at_entry)
helper_entry = {}


def setup_helpers():
    for name, cost in HW_COST.items():
        if name in SIZES and SIZES[name][1]:
            start, size = SIZES[name]
            helper_ranges.append((start, start + size))
            helper_entry[start] = cost
    helper_ranges.sort()


def in_helper(addr):
    for a, b in helper_ranges:
        if a <= addr < b:
            return True
    return False


cache = {}
tally = [0, 0]
prof = {}
hot = {}
counting = [False]
func_starts = []


def func_of(addr, _memo={}):
    f = _memo.get(addr)
    if f is None:
        import bisect
        i = bisect.bisect_right(func_starts, (addr, "\uffff")) - 1
        f = _memo[addr] = func_starts[i][1] if i >= 0 else "?"
    return f


def on_block(uc, addr, size, _):
    key = (addr, size)
    w = cache.get(key)
    if w is None:
        if addr in helper_entry:
            w = (1, helper_entry[addr])        # the hardware instruction it stands for
        elif in_helper(addr):
            w = (0, 0)                         # inside a soft-float helper: not on real hardware
        else:
            code = bytes(uc.mem_read(addr, size))
            ins = list(md.disasm(code, addr))
            w = (len(ins), sum(weight(i) for i in ins))
        cache[key] = w
    tally[0] += w[0]
    tally[1] += w[1]
    if PROFILE and counting[0]:
        f = func_of(addr)
        prof[f] = prof.get(f, 0) + w[1]
        if f.startswith("_ZSt23__is_ios") or "ios_failure" in f:
            hot[addr] = hot.get(addr, 0) + w[1]


def call(mu, fn, *args, floats=()):
    regs = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3]
    sp = SP - 0x40
    for i, a in enumerate(args):
        if i < 4:
            mu.reg_write(regs[i], a & 0xFFFFFFFF)
        else:
            mu.mem_write(sp + 4 * (i - 4), struct.pack("<I", a & 0xFFFFFFFF))
    for i, f in enumerate(floats):
        mu.reg_write(A.UC_ARM_REG_S0 + i, struct.unpack("<I", struct.pack("<f", f))[0])
    mu.reg_write(A.UC_ARM_REG_SP, sp)
    mu.reg_write(A.UC_ARM_REG_LR, STOP | 1)
    tally[0] = tally[1] = 0
    try:
        mu.emu_start(fn | 1, STOP)
    except Exception as err:
        pc = mu.reg_read(A.UC_ARM_REG_PC)
        code = bytes(mu.mem_read(pc & ~1, 8))
        ins = next(md.disasm(code, pc & ~1), None)
        raise SystemExit(f"fault at {pc:#x}: {ins.mnemonic + ' ' + ins.op_str if ins else code.hex()} ({err})")
    return mu.reg_read(A.UC_ARM_REG_R0), tally[0], tally[1]


w = wave.open(WAV)
frames = w.getnframes()
pcm = struct.unpack(f"<{frames * w.getnchannels()}h", w.readframes(frames))
ch = w.getnchannels()
L = struct.pack(f"<{frames}f", *(pcm[i * ch] / 32768 for i in range(frames)))
R = struct.pack(f"<{frames}f", *(pcm[i * ch + ch - 1] / 32768 for i in range(frames)))

for mode in MODES:
    for ratio in RATIOS:
        mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
        for base, size in ((0x08000000, 0x1000000), (0x24000000, 0x1000000), (CLIP, 0x1000000),
                           (0xE0000000, 0x100000)):
            mu.mem_map(base, size)
        mu.mem_write(STOP, b"\x70\x47")
        syms = load_elf(mu, ELF)
        if not helper_ranges:
            setup_helpers()
            # Soft-float helper time counts toward the caller it replaces in hardware.
            func_starts[:] = sorted((a, n) for n, (a, sz) in SIZES.items()
                                    if sz and n not in HW_COST and a >= 0x08000000)
        SP = syms["__stack_top"] - 0x100
        mu.mem_write(CLIP, L)
        mu.mem_write(CLIP + 0x800000, R)
        mu.hook_add(UC_HOOK_BLOCK, on_block)
        call(mu, syms["probe_boot"])
        _, si, sc = call(mu, syms["probe_setup"], mode, CLIP, CLIP + 0x800000, frames, floats=(ratio,))
        heap, _, _ = call(mu, syms["probe_heap_used"])
        per = []
        counting[0] = True
        for b in range(BLOCKS):
            _, ni, nc = call(mu, syms["probe_block"])
            per.append((ni, nc))
        per = per[8:]  # skip warm-up
        mi = sum(p[0] for p in per) / len(per)
        mc = sum(p[1] for p in per) / len(per)
        pk = max(p[1] for p in per)
        name = os.environ.get("LABEL") or ["faster (R2)", "finer (R3)", "finer+short"][mode]
        print(f"{name:12} ratio {ratio:4}: {mi / 1e6:5.2f}M insns/block, ~{mc / 1e6:5.2f}M cycles "
              f"= {100 * mc / BUDGET:5.1f}% avg, peak {100 * pk / BUDGET:5.1f}% | heap {heap / 1024:.0f} KB, "
              f"setup {si / 1e6:.1f}M insns", flush=True)
        if os.environ.get("PERBLOCK"):
            print("    per-block Mcycles:", " ".join(f"{p[1] / 1e6:.2f}" for p in per))
        if PROFILE:
            tot = sum(prof.values())
            for f, c in sorted(prof.items(), key=lambda x: -x[1])[:18]:
                print(f"    {100 * c / tot:5.1f}%  {f}")
            prof.clear()
            for a_, c in sorted(hot.items(), key=lambda x: -x[1])[:6]:
                print(f"    hot {a_:#x} {c}")
        counting[0] = False
