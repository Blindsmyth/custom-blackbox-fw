#!/usr/bin/env python3
"""Cycle estimate of the master chain on the Blackbox Cortex-M7 (Unicorn).

    tools/master_chain/m7/build.sh
    .venv/bin/python tools/master_chain/m7/measure.py tools/master_chain/m7/probe.elf in_f32.wav

Same model as tools/lab/rb/m7/measure.py: every instruction 1 cycle, VDIV/VSQRT.F32 14,
.F64 arithmetic 2-3, load/store-multiple per register. Ignores dual issue (cheaper) and
cache misses / SDRAM waits (dearer). Budget: 480 MHz x 5.33 ms = 2.56M cycles per 256 frames.
"""
import os
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn import arm_const as A

ELF, WAV = sys.argv[1], sys.argv[2]
BLOCKS = int(os.environ.get("BLOCKS", "120"))
BUDGET = 480e6 * 256 / 48000
CLIP, STOP = 0xC0000000, 0x08FFFF00
STAGES = ["whole chain (Squish)", "CLR alone", "CMP Squish alone", "MAX alone",
          "whole chain (CMP Stock)", "chain off (stock comp + limiter)"]
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)


def read_f32_wav(path, limit):
    data = open(path, "rb").read()
    i, fmt, ch = 12, None, 2
    while i < len(data) - 8:
        cid, size = data[i:i + 4], struct.unpack_from("<I", data, i + 4)[0]
        if cid == b"fmt ":
            fmt, ch = struct.unpack_from("<HH", data, i + 8)
        elif cid == b"data":
            n = min(size // (4 * ch), limit)
            s = struct.unpack_from(f"<{n * ch}f", data, i + 8)
            return s[0::ch], s[ch - 1::ch]
        i += 8 + size + (size & 1)
    raise SystemExit("no float32 data chunk")


def load_elf(mu, path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    for k in range(phnum):
        p_type, p_off, p_vaddr, _pa, p_filesz, _m, _f, _a = struct.unpack_from("<IIIIIIII", data, phoff + k * phentsize)
        if p_type == 1 and p_filesz:
            mu.mem_write(p_vaddr, data[p_off:p_off + p_filesz])
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, _ = struct.unpack_from("<HHH", data, 46)
    secs = [struct.unpack_from("<IIIIIIIIII", data, shoff + k * shentsize) for k in range(shnum)]
    syms = {}
    for s in secs:
        if s[1] == 2:
            strtab = secs[s[6]]
            for k in range(s[5] // 16):
                name, value = struct.unpack_from("<II", data, s[4] + k * 16)
                n = data[strtab[4] + name:].split(b"\0", 1)[0].decode()
                if n:
                    syms[n] = value
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


cache, tally = {}, [0]


def on_block(uc, addr, size, _):
    w = cache.get((addr, size))
    if w is None:
        w = cache[(addr, size)] = sum(weight(i) for i in md.disasm(bytes(uc.mem_read(addr, size)), addr))
    tally[0] += w


def call(mu, sp, fn, *args):
    regs = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3]
    for k, a in enumerate(args):
        mu.reg_write(regs[k], a & 0xFFFFFFFF)
    mu.reg_write(A.UC_ARM_REG_SP, sp)
    mu.reg_write(A.UC_ARM_REG_LR, STOP | 1)
    tally[0] = 0
    mu.emu_start(fn | 1, STOP)
    return mu.reg_read(A.UC_ARM_REG_R0), tally[0]


L, R = read_f32_wav(WAV, BLOCKS * 256)
frames = len(L)
for stage, name in enumerate(STAGES):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
    for base, size in ((0x08000000, 0x1000000), (0x24000000, 0x1000000), (CLIP, 0x1000000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)
    mu.mem_write(STOP, b"\x70\x47")
    # enable the FPU (CPACR CP10/CP11 full access)
    mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    syms = load_elf(mu, ELF)
    sp = syms["__stack_top"] - 0x100
    mu.mem_write(CLIP, struct.pack(f"<{frames}f", *L))
    mu.mem_write(CLIP + 0x800000, struct.pack(f"<{frames}f", *R))
    mu.hook_add(UC_HOOK_BLOCK, on_block)
    if stage == 0:
        sz = [call(mu, sp, syms["probe_sizes"], k)[0] for k in range(3)]
        print(f"state: CLR {sz[0]} B, CMP {sz[1]} B, MAX {sz[2]} B (total {sum(sz)} B)")
        print(f"{'stage':36s} {'mean':>9s} {'max':>9s}  of budget (mean / max)")
    call(mu, sp, syms["probe_boot"])
    call(mu, sp, syms["probe_setup"], stage, CLIP, CLIP + 0x800000, frames)
    per = [call(mu, sp, syms["probe_block"])[1] for _ in range(BLOCKS)]
    mean, peak = sum(per) / len(per), max(per)
    print(f"{name:36s} {mean:9.0f} {peak:9.0f}  {100 * mean / BUDGET:5.1f}% / {100 * peak / BUDGET:5.1f}%")
