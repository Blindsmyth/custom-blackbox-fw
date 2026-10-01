"""Off-device test bench for BLACKBOX.BIN 3.1.9 (stock or patched).

Runs firmware functions in Unicorn (Cortex-M7, Thumb-2 + FPU). The four FatFs diskio
dispatchers are replaced by Python functions backed by a FAT32 card image, so the
firmware's own FatFs, SDMgr and PresetMgr code runs unmodified against a real
filesystem. Nothing here touches a real SD card.

Addresses are 3.1.9 addresses (docs/map-319.md).
"""
import struct

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn import arm_const as A

BASE = 0x08040000
STOP = 0x080FFF00          # return address that ends a call (inside mapped flash, never code)

# 3.1.9 addresses
DISK_INITIALIZE = 0x080840F0
DISK_STATUS = 0x080840D8
DISK_READ = 0x08084118
DISK_WRITE = 0x08084138
DISK_IOCTL = 0x08084158
GET_FATTIME = 0x08084174
F_MOUNT = 0x080864E8
LOG_PRINTF = 0x08043DE4
SDMGR_CHECKMOUNT = 0x08042C90
STREAMER_WAKE = 0x080711E0     # thunk target used by every PresetMgr_Request*
SD_DRIVE = 0x240021A4
SD_MOUNT_OBJ = 0x24009BFC      # *obj = FATFS*
PRESETMGR = 0x2401F508
ENGINE_OBJ = 0x2400A9C0
ENGINE_INIT = 0x08070BF4       # streaming engine init; allocates PresetMgr lists and XML buffers
LIST_GET = 0x0804290C
BUILD_LIST = 0x08091BB8
PRESET_LOAD = 0x08091988
DOC_OPEN = 0x08096AD8
RESET_TABLE = 0x08040504
INIT_ARRAY = (0x080CAB00, 0x080CAB8C)

SCRATCH = 0x2001C000           # bench-only scratch in DTCM (patch state lives at 0x20000000)
BENCH_FATFS = 0x2001D000
BENCH_FIL = 0x2001D400
SHARED_FIL_PTR = 0x24020084
BENCH_SP = 0x2001F7F0


class StopCall(Exception):
    pass


class Board:
    def __init__(self, image_path, card_path, allow_writes=False):
        self.image = open(image_path, "rb").read()
        self.card = bytearray(open(card_path, "rb").read())
        self.card_orig = bytes(self.card)
        self.allow_writes = allow_writes
        self.writes = []          # (sector, count)
        self.reads = 0
        self.log = []
        self.calls = {}           # addr -> count, for watched functions
        self.stubs = {}
        self.unmapped = []
        mu = self.mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        mu.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M7)
        for base, size in ((0x00000000, 0x10000), (0x08000000, 0x100000), (0x20000000, 0x20000),
                           (0x24000000, 0x80000), (0x30000000, 0x48000), (0x38000000, 0x10000),
                           (0x38800000, 0x1000), (0xC0000000, 0x4000000), (0xE0000000, 0x100000)):
            mu.mem_map(base, size)
        mu.mem_write(BASE, self.image)
        mu.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self._hooked = set()
        self._install_default_stubs()

    # ---- memory helpers
    def u32(self, a):
        return struct.unpack("<I", self.mu.mem_read(a, 4))[0]

    def w32(self, a, v):
        self.mu.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def u8(self, a):
        return self.mu.mem_read(a, 1)[0]

    def cstr(self, a, n=256):
        b = bytes(self.mu.mem_read(a, n))
        return b.split(b"\0", 1)[0].decode("latin1")

    def put_cstr(self, a, s):
        self.mu.mem_write(a, s.encode("latin1") + b"\0")
        return a

    def img_u32(self, a):
        return struct.unpack_from("<I", self.image, a - BASE)[0]

    # ---- hooks
    def _unmapped(self, mu, access, addr, size, value, data):
        page = addr & ~0xFFFF
        self.unmapped.append((hex(mu.reg_read(A.UC_ARM_REG_PC)), hex(addr)))
        if 0x40000000 <= addr < 0x60000000:
            mu.mem_map(page, 0x10000)
            return True
        return False

    def _hook_at(self, addr):
        if addr not in self._hooked:
            self._hooked.add(addr)
            self.mu.hook_add(UC_HOOK_CODE, self._code_hook, begin=addr, end=addr)

    def stub(self, addr, fn):
        """Replace the function at addr: fn(board) returns r0 (or None) and the call returns."""
        self.stubs[addr & ~1] = fn
        self._hook_at(addr & ~1)

    def unstub(self, addr):
        self.stubs.pop(addr & ~1, None)

    def watch(self, addr):
        """Count how often execution reaches addr."""
        self.calls[addr & ~1] = 0
        self._hook_at(addr & ~1)

    def _code_hook(self, mu, addr, size, data):
        if addr in self.calls:
            self.calls[addr] += 1
        fn = self.stubs.get(addr)
        if fn is None:
            return
        r = fn(self)
        if r is not None:
            mu.reg_write(A.UC_ARM_REG_R0, r & 0xFFFFFFFF)
        mu.reg_write(A.UC_ARM_REG_PC, mu.reg_read(A.UC_ARM_REG_LR) | 1)

    def arg(self, i):
        if i < 4:
            return self.mu.reg_read(A.UC_ARM_REG_R0 + i)
        return self.u32(self.mu.reg_read(A.UC_ARM_REG_SP) + 4 * (i - 4))

    # ---- diskio backed by the card image
    def _install_default_stubs(self):
        self.stub(DISK_INITIALIZE, lambda b: 0)
        self.stub(DISK_STATUS, lambda b: 0)
        self.stub(DISK_READ, Board._disk_read)
        self.stub(DISK_WRITE, Board._disk_write)
        self.stub(DISK_IOCTL, Board._disk_ioctl)
        self.stub(GET_FATTIME, lambda b: (46 << 25) | (9 << 21) | (30 << 16))
        self.stub(LOG_PRINTF, Board._log)
        self.stub(SDMGR_CHECKMOUNT, Board._checkmount)
        self.stub(STREAMER_WAKE, lambda b: None)

    def _disk_read(self):
        buf, sector, count = self.arg(1), self.arg(2), self.arg(3)
        self.reads += count
        self.mu.mem_write(buf, bytes(self.card[sector * 512:(sector + count) * 512]))
        return 0

    def _disk_write(self):
        buf, sector, count = self.arg(1), self.arg(2), self.arg(3)
        self.writes.append((sector, count))
        if not self.allow_writes:
            return 2   # RES_WRPRT: firmware sees a write-protected card
        self.card[sector * 512:(sector + count) * 512] = bytes(self.mu.mem_read(buf, count * 512))
        return 0

    def _disk_ioctl(self):
        cmd, buf = self.arg(1), self.arg(2)
        if cmd == 1:
            self.w32(buf, len(self.card) // 512)
        elif cmd == 2:
            self.mu.mem_write(buf, struct.pack("<H", 512))
        elif cmd == 3:
            self.w32(buf, 1)
        return 0

    def _log(self):
        fmt = self.arg(0)
        if 0x08040000 <= fmt < 0x08100000:
            self.log.append(self.cstr(fmt, 120))
        return 0

    def _checkmount(self):
        self.mu.mem_write(SD_DRIVE + 0x15, b"\x01")
        return 0

    # ---- boot-equivalent state
    def reset_init(self):
        """Copy .data, zero .bss, set up the heap block and run the C++ constructors."""
        t = RESET_TABLE
        lit = [self.img_u32(t + 4 * i) for i in range(32)]
        # literal layout (see Reset_Handler): 7 copy triples at +0x10, 2 zero pairs at +0x64, heap at +0x74
        for i in range(7):
            src, dst, end = lit[4 + 3 * i], lit[5 + 3 * i], lit[6 + 3 * i]
            if src != dst and end > dst:
                self.mu.mem_write(dst, bytes(self.mu.mem_read(src, end - dst)))
        for i in range(2):
            lo, hi = lit[25 + 2 * i], lit[26 + 2 * i]
            if hi > lo:
                self.mu.mem_write(lo, b"\0" * (hi - lo))
        heap_lo, heap_hi = lit[29], lit[30]
        self.w32(heap_lo, 0)
        self.w32(heap_lo + 4, heap_hi - heap_lo)
        for p in range(*INIT_ARRAY, 4):
            self.call(self.img_u32(p) & ~1)

    def mount(self):
        """f_mount the card image on volume 0, the way SDMgr does at startup."""
        fs = self.u32(SD_MOUNT_OBJ) or BENCH_FATFS
        path = self.put_cstr(SCRATCH, "0:/")
        r = self.call(F_MOUNT, fs, path, 1)
        self.mu.mem_write(SD_DRIVE + 0x15, b"\x01")
        return r

    # ---- calling firmware functions
    def call(self, addr, *args, limit=200_000_000):
        mu = self.mu
        regs = list(args[:4]) + [0] * (4 - min(4, len(args)))
        sp = BENCH_SP
        extra = list(args[4:])
        sp -= 4 * len(extra)
        sp &= ~7
        for i, v in enumerate(extra):
            self.w32(sp + 4 * i, v)
        for i, v in enumerate(regs):
            mu.reg_write(A.UC_ARM_REG_R0 + i, v & 0xFFFFFFFF)
        mu.reg_write(A.UC_ARM_REG_SP, sp)
        mu.reg_write(A.UC_ARM_REG_LR, STOP | 1)
        try:
            mu.emu_start(addr | 1, STOP, count=limit)
        except UcError as e:
            pc = mu.reg_read(A.UC_ARM_REG_PC)
            raise RuntimeError(f"emulation fault at 0x{pc:08x} calling 0x{addr:08x}: {e}; "
                               f"unmapped={self.unmapped[-3:]}") from None
        if mu.reg_read(A.UC_ARM_REG_PC) != STOP:
            raise RuntimeError(f"call 0x{addr:08x} did not return (pc=0x{mu.reg_read(A.UC_ARM_REG_PC):08x})")
        return mu.reg_read(A.UC_ARM_REG_R0)

    # ---- snapshots of RAM (flash is never written by firmware code)
    RAM = ((0x00000000, 0x10000), (0x20000000, 0x20000), (0x24000000, 0x80000),
           (0x30000000, 0x48000), (0x38000000, 0x10000), (0x38800000, 0x1000),
           (0xC0000000, 0x4000000), (0xE0000000, 0x100000))

    def snapshot(self):
        return [bytes(self.mu.mem_read(a, n)) for a, n in self.RAM]

    def restore(self, snap):
        for (a, n), data in zip(self.RAM, snap):
            self.mu.mem_write(a, data)

    def boot(self):
        """Boot-equivalent state: C runtime init, engine/PresetMgr init, card mounted."""
        self.reset_init()
        self.call(ENGINE_INIT, ENGINE_OBJ)
        if self.u32(SHARED_FIL_PTR) == 0:
            self.w32(SHARED_FIL_PTR, BENCH_FIL)
        self.mount()

    # ---- card checks
    def changed_sectors(self):
        out = []
        for s in range(0, len(self.card), 512):
            if self.card[s:s + 512] != self.card_orig[s:s + 512]:
                out.append(s // 512)
        return out
