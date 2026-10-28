#!/usr/bin/env python3
"""Bench tests for BLACKBOX.BIN 3.1.9, stock or patched.

    .venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
    .venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-preset-folders/BLACKBOX.BIN
    .venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-clip-repitch/BLACKBOX.BIN
    .venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-folders-repitch/BLACKBOX.BIN

The UI side is modelled at the PresetMgr boundary: a Load press, the pcmStreamer
command dispatcher, and the UI event pop. SessionMgr_LoadBank and the screen switch
are stubbed and recorded; a recorded LoadBank becomes a real PresetMgr_RequestLoad,
which is what the stock LoadBank does after clearing the pads.

Every test runs on a fresh card image. The card is write-protected (disk_write
returns RES_WRPRT and is recorded), so any attempted write fails the test, and the
image is checked with fsck.fat -n afterwards. Tests of the writing commands (Save As,
Delete, Rename, New, Clean) run with writes allowed and check the exact set of files
that changed instead.
"""
import argparse
import json
import struct
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import board as bd  # noqa: E402
import mkcard  # noqa: E402

VERSION_OFF = 0x8F294
VERSION_REF = 0x0809AF30   # pool word the main menu draws the version from (tools/version_text.py)
STOCK_LEN = 728696


def menu_letter(image):
    """Build letter as the menu shows it. Since 2026-10-15 the '9' at VERSION_OFF stays and the version
    pool words point at "3.1.<letter>" after the cave; older images overwrote the '9'."""
    if image[VERSION_OFF] != ord("9"):
        return chr(image[VERSION_OFF])
    ptr = int.from_bytes(image[VERSION_REF - 0x08040000:VERSION_REF - 0x08040000 + 4], "little")
    off = ptr - 0x08040000
    if 0 <= off < len(image):
        text = bytes(image[off:off + 8]).split(b"\0", 1)[0]
        if text.startswith(b"3.1."):
            return text[4:5].decode()
    return "9"


def is_patched(image):
    return len(image) > STOCK_LEN or menu_letter(image) != "9"
DISPATCH = 0x08092F5C
POP_EVENT = 0x080905D0
REQUEST_LOAD = 0x08090B78
REQUEST_LIST = 0x0809067C
LOAD_BANK = 0x0809E218
SCREEN_SWITCH = 0x0809EAEC
PRESET_PATH_JOIN = 0x080915A0
STR_CSTR = 0x080460BA
DTCM_ALLOC = 0x080443A8   # bump allocator: small blocks from 0x20000000 up, then malloc
REQUEST_SAVE = 0x08090A48
REQUEST_SAVE_AS = 0x08090AD8
REQUEST_RENAME = 0x08090F0C
REQUEST_NEW = 0x08090FBC
REQUEST_DELETE = 0x080910BC
REQUEST_CLEAN = 0x080911C0
SESSION_COPY = 0x08090704  # RequestSaveAs copies the app's session; the bench saves the loaded one
UI_POST_UP = 0x080AED14
NAME2_BUF = 0x2001C500

PM = bd.PRESETMGR
DISPLAY_LIST = PM + 0x32C
FAKE_APP = 0x2001E000
NAME_BUF = 0x2001C400
EVT_BUF = 0x2001C600
PATH_BUF = 0x2001C700

STATE = 0x38000000
S_BROWSE = STATE + 0x100
S_BASE = STATE + 0x200
S_PEND = STATE + 0x01C    # byte: the shown folder has rows left to check (hook_idle)

TREE = dict(mkcard.DEFAULT_TREE)
TREE["Root.xml"] = mkcard.PRESET_XML  # legacy root preset: listed at the top level only


class Bench:
    def __init__(self, image, card, allow_writes=False):
        self.card_path = card
        self.image_path = str(image)
        self.b = bd.Board(image, card, allow_writes=allow_writes)
        self.sym = {}
        if is_patched(self.b.image):
            symfile = Path(image).with_suffix(".sym.json")
            self.sym = {k: int(v, 16) for k, v in json.loads(symfile.read_text()).items()}
        self.folders = "fm_init" in self.sym
        self.dotdot = "pset_on_event" not in self.sym   # 3.1.Q: ^ button instead of a ".." row
        self.repitch = "hook_register" in self.sym
        self.patched = self.folders
        self.idle = "hook_idle" in self.sym
        self.idle_reads = []      # sector reads of each idle dispatcher step (mark_step)
        self.loadbank = []
        self.screen = []
        self.events = []
        self.b.stub(LOAD_BANK, self._loadbank)
        self.b.stub(SCREEN_SWITCH, lambda b: self.screen.append((b.arg(0), b.arg(1))))
        self.b.boot()
        if self.folders:
            self.b.call(self.sym["fm_init"])
        self.fill_dtcm()

    def fill_dtcm(self, blocks=64, size=0x400):
        """Allocate and scribble DTCM through the stock allocator, as the app's init does on the
        device after the patch state is set up. Patch state must survive this."""
        for _ in range(blocks):
            p = self.b.call(DTCM_ALLOC, size)
            self.b.mu.mem_write(p, b"\xa5" * size)

    def _loadbank(self, b):
        self.loadbank.append((b.arg(0), b.arg(1), b.arg(2)))

    # ---- helpers
    def rows(self):
        out = []
        i = 0
        while self.b.call(bd.LIST_GET, DISPLAY_LIST, i, NAME_BUF):
            out.append(self.b.cstr(NAME_BUF))
            i += 1
        return out

    def str_at(self, obj):
        return self.b.cstr(self.b.call(STR_CSTR, obj))

    def current(self):
        return self.str_at(PM + 0x394)

    def browse(self):
        return self.b.cstr(S_BROWSE)

    def base(self):
        return self.b.cstr(S_BASE)

    def join(self, comp):
        self.b.put_cstr(NAME_BUF, comp)
        self.b.call(PRESET_PATH_JOIN, PATH_BUF, PM, NAME_BUF)
        return self.b.cstr(PATH_BUF)

    def files(self):
        """Write the emulated card back to its image file and list it."""
        with open(self.card_path, "wb") as f:
            f.write(self.b.card)
        return mkcard.listing(self.card_path)

    def request(self, addr, *names):
        """Call a PresetMgr_Request* with C-string arguments and let the streamer run it."""
        bufs = [NAME_BUF, NAME2_BUF]
        args = [self.b.put_cstr(bufs[i], n) for i, n in enumerate(names)]
        self.events.clear()
        self.b.call(addr, PM, *args)
        self.pump()

    def save_as(self, new):
        """The UI's Save As: RequestSaveAs(new), then RequestLoad(new)."""
        self.b.stub(SESSION_COPY, lambda b: 0)
        self.b.put_cstr(NAME_BUF, new)
        self.events.clear()
        self.b.call(REQUEST_SAVE_AS, PM, NAME_BUF, 0, 1)
        self.pump()
        self.b.unstub(SESSION_COPY)
        self.b.call(REQUEST_LOAD, PM, self.b.put_cstr(NAME_BUF, new))
        self.pump()

    def nested(self, rows):
        """Expected rows below the top level: with a leading ".." unless the image dropped it."""
        return ([".."] if self.dotdot else []) + rows

    def up(self):
        """Go up one folder: the ".." row where it exists, else BACK (the ^ button)."""
        if self.dotdot:
            self.press_load("..")
        else:
            self.back()

    def back(self):
        """Press BACK on the preset screen. Returns True when the screen was left."""
        self.b.mu.mem_write(0x24020088 + 0x8CA4, b"\x07")    # on the preset screen
        self.b.w32(0x24015CE4, self.b.u32(0x24015CE4) + 1000)  # past the 300 ms debounce
        left = []
        self.b.stub(UI_POST_UP, lambda b: left.append(b.arg(1)) or 0)
        self.b.call(self.sym["hook_back"], FAKE_APP, EVT_BUF)
        self.b.unstub(UI_POST_UP)
        self.pump()
        return bool(left)

    # ---- the two tasks
    def pump(self):
        """Run the streamer until its queue is empty, then pop UI events; repeat."""
        for _ in range(20):
            busy = False
            while self.b.u32(PM + 0x120) != self.b.u32(PM + 0x124):
                self.b.call(DISPATCH, PM)
                busy = True
            # pcmStreamer calls the dispatcher on every loop, ring empty or not; with folder rows
            # left to check, each call checks a few of them (hook_idle -> mark_step)
            steps = 0
            while self.idle and self.b.u8(S_PEND) and self.b.u32(PM + 0x120) == self.b.u32(PM + 0x124):
                before = self.b.reads
                self.b.call(DISPATCH, PM)
                self.idle_reads.append(self.b.reads - before)
                busy = True
                steps += 1
                if steps > 2000:
                    raise RuntimeError("idle mark steps did not finish")
            while self.b.call(POP_EVENT, PM, EVT_BUF):
                self.events.append((self.b.u8(EVT_BUF), self.b.u32(EVT_BUF + 8), self.b.u32(EVT_BUF + 0x10)))
                busy = True
            while self.loadbank:
                app, idx, flag = self.loadbank.pop(0)
                if self.b.call(bd.LIST_GET, DISPLAY_LIST, idx, NAME_BUF):
                    self.b.call(REQUEST_LOAD, PM, NAME_BUF)
                busy = True
            if not busy:
                return
        raise RuntimeError("pump did not settle")

    def refresh(self):
        self.b.call(REQUEST_LIST, PM)
        self.pump()

    def press_load(self, name):
        """Press Load on the row called name, as the preset screen does."""
        rows = self.rows()
        idx = rows.index(name)
        self.events.clear()
        self.screen.clear()
        if self.patched:
            self.b.call(self.sym["hook_ui_load"], FAKE_APP, idx, 1)
        else:
            self.loadbank.append((FAKE_APP, idx, 1))
            self.screen.append((FAKE_APP, 2))
        self.pump()
        return idx

    def load_ok(self):
        return [e for e in self.events if e[0] == 3 and e[2] == 1]


class Result:
    def __init__(self):
        self.failed = 0
        self.passed = 0

    def check(self, cond, what):
        if cond:
            self.passed += 1
            print(f"  ok    {what}")
        else:
            self.failed += 1
            print(f"  FAIL  {what}")


def itcm_watch(bench):
    """3.1.R runs Rubber Band's hot code from ITCM (0x00001000-0x0000FFFF). Stock code must never
    write anywhere in ITCM, nor read our part of it. Reads below 0x1000 are null-pointer reads
    (str_is_empty on an empty string reads address 0); they hit ITCM on a stock unit too, and the
    4 KB guard keeps them off our code."""
    if "rb_pair" not in bench.sym:
        return None
    from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
    seen = []

    def on(mu, access, addr, size, value, data):
        pc = mu.reg_read(bd.A.UC_ARM_REG_PC)
        if 0x08040000 <= pc < 0x080F1E80 and len(seen) < 8 and (access == UC_MEM_WRITE or addr >= 0x1000):
            seen.append(("write" if access == UC_MEM_WRITE else "read", hex(pc), hex(addr)))

    bench.b.mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on, begin=0, end=0xFFFF)
    return seen


def run_case(image, name, fn, res, tree=None, writes=False, fs="fat"):
    print(f"[{name}]")
    with tempfile.TemporaryDirectory() as tmp:
        card = os.path.join(tmp, "card.img")
        # Save As only copies samples with 100 MB to spare
        if fs == "exfat":
            mkcard.build_exfat(card, tree or TREE, size_mb=160, cluster_kb=32)
        else:
            mkcard.build_cached(card, tree or TREE, size_mb=160 if writes else 64)
        bench = Bench(image, card, allow_writes=writes)
        itcm = itcm_watch(bench)
        try:
            fn(bench, res)
        except Exception as e:  # a crash in emulation is a failure, not a bench error
            res.check(False, f"no emulation fault ({e})")
        if itcm is not None:
            res.check(not itcm, f"stock code never writes ITCM nor reads our part ({itcm[:2]})")
        if not writes:
            res.check(not bench.b.writes, f"no disk writes attempted ({len(bench.b.writes)})")
            res.check(not bench.b.changed_sectors(), "card image unchanged")
        with open(card, "wb") as f:
            f.write(bench.b.card)
        ok, msg = mkcard.fsck(card)
        res.check(ok, "fsck.fat -n clean" + ("" if ok else f": {msg[-200:]}"))


# ---- tests shared by stock and patched images
def t_list_top(bench, res):
    bench.refresh()
    rows = bench.rows()
    group = "/_Test" if bench.patched else "_Test"
    res.check(rows == [group, "Alpha", "Beta", "Root", "Zeta Kit"], f"top-level rows {rows}")
    ids = [e[0] for e in bench.events]
    res.check(0x11 in ids, "list-done event posted")


def t_load_real(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    res.check(len(bench.load_ok()) == 1, "Alpha: load event with success")
    res.check(bench.current() == "Alpha", f"current preset is {bench.current()!r}")
    res.check(bench.screen == [(FAKE_APP, 2)], "screen switched away from the preset list")


def t_stock_group_fallback(bench, res):
    bench.refresh()
    bench.press_load("_Test")
    res.check(len(bench.load_ok()) == 1, "group row: stock loads a fallback preset")
    res.check(bench.current() == "Alpha", f"fallback preset is {bench.current()!r} (the stock bug)")


# ---- clip-repitch tests (3.1.W; stock listing, no folder hooks)
ID_REPITCH = 0x19C
PARAM_REGISTER_ALL = 0x0808C158
PARAM_XML_NAME = 0x0808E33C
PARAM_TITLE = 0x0808E220
PARAM_SET = 0x08093ECC
PARAM_ADD = 0x08093EF6
PARAM_GET = 0x08093E9C
REG_PTR = 0x2401F4FC
BAG_SCRATCH = 0x2001C800
BAG_PAIRS = 0x2001C840
CLIP_POS = 0x080EF990
SLICER_POS = 0x080EF828
SAMPLE_POS = 0x080EFC60


def _u16s(img, va, n):
    off = va - bd.BASE
    return [int.from_bytes(img[off + i * 2:off + i * 2 + 2], "little") for i in range(n)]


def t_repitch_pos_id(bench, res):
    img = bench.b.image
    clip = _u16s(img, CLIP_POS, 6)
    res.check(clip[:5] == [0x8D, 0xD2, 0x47, 0x94, ID_REPITCH] and clip[5] in (0, 0x19D, 0x1A1, ID_CUEMODE),
              f"Clip Pos ids {clip}")
    slicer = _u16s(img, SLICER_POS, 9)
    res.check(slicer == [0x8D, 0xF2, 0xD2, 0xF3, 0xF4, 0x4E, 0xA2, ID_REPITCH, 0],
              f"Slicer Pos ids {slicer}")
    sample = _u16s(img, SAMPLE_POS, 8)
    res.check(ID_REPITCH not in sample, f"Sample Pos unchanged {sample}")


def t_repitch_registry(bench, res):
    if bench.b.u32(REG_PTR) == 0:
        bench.b.call(PARAM_REGISTER_ALL, 0)
    res.check(bench.b.u32(REG_PTR) != 0, "param registry allocated")
    name = bench.b.call(PARAM_XML_NAME, ID_REPITCH)
    title = bench.b.call(PARAM_TITLE, ID_REPITCH)
    res.check(name != 0 and bench.b.cstr(name) == "repitch", f"xml name {name:#x}")
    res.check(title != 0 and bench.b.cstr(title) == "Warp:", f"title {title:#x}")


def t_repitch_set_insert(bench, res):
    b = bench.b
    b.w32(BAG_SCRATCH + 4, BAG_PAIRS)
    b.mu.mem_write(BAG_SCRATCH + 10, b"\x00\x00\x10\x00")
    got = b.call(PARAM_GET, BAG_SCRATCH, ID_REPITCH)
    res.check(got == 0, f"empty bag get {got}")
    b.call(PARAM_SET, BAG_SCRATCH, ID_REPITCH, 1)
    got = b.call(PARAM_GET, BAG_SCRATCH, ID_REPITCH)
    res.check(got == 1, f"set-insert 0x19C -> {got}")
    b.call(PARAM_SET, BAG_SCRATCH, ID_REPITCH, 0)
    got = b.call(PARAM_GET, BAG_SCRATCH, ID_REPITCH)
    res.check(got == 0, f"set existing 0x19C -> {got}")


def t_repitch_clip_flag(bench, res):
    b = bench.b
    clip = 0x2001C800
    tempo = 0x2001CC00
    msg = 0x2001CD00
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(tempo, b"\x00" * 0x20)
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(clip + 0x18, 0x100)
    b.w32(msg + 0xC, ID_REPITCH)
    b.w32(msg + 0x10, 1)
    b.call(bench.sym["state_init"])
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 1, f"clip +0xBB1 is {flag}")
    b.w32(msg + 0x10, 0)
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 0, f"clip +0xBB1 cleared {flag}")


def t_repitch_main_rate_preserves_r3(bench, res):
    """hook_main_rate must leave r3 alone: it is the grain pointer at 0x08065756."""
    b = bench.b
    clip = 0x2001C800
    grain = 0x2001D000
    A = bd.A
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(grain, b"\x00" * 0x80)
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x40000000)  # 2.0
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    stored = b.u32(grain + 0x48)
    res.check(r3 == grain, f"r3 clobbered to {r3:#x}")
    res.check(stored == 0x40000000, f"stretch rate {stored:#x}")
    b.mu.mem_write(clip + 0xBB1, b"\x01")
    b.w32(clip + 0xBB4, 0x3F000000)  # 0.5
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x40000000)
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    stored = b.u32(grain + 0x48)
    res.check(r3 == grain, f"r3 clobbered on repitch {r3:#x}")
    if "rb_pair" in bench.sym and leaf_v(bench) == 5:
        # 3.1.T: value 1 is Fill, which renders from the ratio in bank 2: grain rate unscaled
        res.check(stored == 0x40000000, f"Fill leaves the rate {stored:#x}")
        b.mu.mem_write(clip + 0xBB1, b"\x02")              # Repitch
        b.mu.reg_write(A.UC_ARM_REG_R3, grain)
        b.mu.reg_write(A.UC_ARM_REG_R4, clip)
        b.mu.reg_write(A.UC_ARM_REG_S15, 0x40000000)
        b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
        b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
        b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
        res.check(b.u32(grain + 0x48) == 0x3F800000, f"Repitch (2) rate {b.u32(grain + 0x48):#x}")
    elif "rb_pair" in bench.sym and leaf_v(bench) == 1:
        # 3.1.R: the list is Stretch / Rubber, so value 1 is Rubber: grain rate unscaled, voice reset
        res.check(stored == 0x40000000, f"rubber leaves the rate {stored:#x}")
        res.check(b.u32(0x38007000 + 20) == 1, "rubber: a new grain asks the voice to reset")
    else:
        res.check(stored == 0x3F800000, f"repitch rate {stored:#x}")
    if "rb_pair" in bench.sym and leaf_v(bench) == 2:
        # 3.1.B --with-repitch: value 2 is the leaf: rate unscaled, voice reset, the voice taken
        b.w32(0x38007000 + 20, 0)
        b.mu.mem_write(clip + 0xBB1, b"\x02")
        b.mu.reg_write(A.UC_ARM_REG_R3, grain)
        b.mu.reg_write(A.UC_ARM_REG_R4, clip)
        b.mu.reg_write(A.UC_ARM_REG_S15, 0x40000000)
        b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
        b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
        b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
        res.check(b.u32(grain + 0x48) == 0x40000000 and b.u32(0x38007000 + 20) == 1,
                  "leaf at value 2: rate unscaled, a new grain asks the voice to reset")
        res.check(b.u32(0x38007000 + 4) == clip, "leaf at value 2: the launch takes the voice")


CLIP_GRAIN = 0x08065290


def t_repitch_slicer_grain_stack(bench, res):
    """hook_grain must not push before FUN_08065290: args 5-10 are on the stack."""
    b = bench.b
    seen = {}

    def on_grain(board):
        seen["r0"] = board.arg(0)
        seen["r1"] = board.arg(1)
        seen["r2"] = board.arg(2)
        seen["r3"] = board.arg(3)
        seen["a5"] = board.arg(4)
        seen["a6"] = board.arg(5)
        seen["a7"] = board.arg(6)
        seen["a8"] = board.arg(7)
        seen["a9"] = board.arg(8)
        seen["a10"] = board.arg(9)
        return 2

    b.stub(CLIP_GRAIN, on_grain)
    clip = 0x2001C800
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.w32(clip + 0xE0 + 0x48, 0x40000000)  # grain 2 rate 2.0
    b.w32(clip + 0xE0 + 0x60, 0x12345678)
    sp = bd.BENCH_SP - 0x20
    stack_args = [0x51, 0x52, 0x53, 0x54, 0x55, 0x56]
    for i, v in enumerate(stack_args):
        b.w32(sp + i * 4, v)
    A = bd.A
    b.mu.reg_write(A.UC_ARM_REG_R0, clip)
    b.mu.reg_write(A.UC_ARM_REG_R1, 0x11)
    b.mu.reg_write(A.UC_ARM_REG_R2, 0x12)
    b.mu.reg_write(A.UC_ARM_REG_R3, 0x13)
    b.mu.reg_write(A.UC_ARM_REG_SP, sp)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_grain"] | 1, bd.STOP, count=400)
    res.check(seen.get("r0") == clip, f"grain r0 {seen.get('r0')}")
    res.check(seen.get("r1") == 0x11, f"grain r1 {seen.get('r1')}")
    res.check(seen.get("r2") == 0x12, f"grain r2 {seen.get('r2')}")
    res.check(seen.get("r3") == 0x13, f"grain r3 {seen.get('r3')}")
    res.check([seen.get(k) for k in ("a5", "a6", "a7", "a8", "a9", "a10")] == stack_args,
              f"grain stack args {[seen.get(k) for k in ('a5', 'a6', 'a7', 'a8', 'a9', 'a10')]}")
    r0 = b.mu.reg_read(A.UC_ARM_REG_R0)
    res.check(r0 == 2, f"grain index {r0}")
    res.check(b.u32(clip + 0xE0 + 0x48) == 0x40000000, "stretch leaves grain rate")
    b.mu.mem_write(clip + 0xBB1, b"\x01")
    b.w32(clip + 0xBB4, 0x3F000000)  # 0.5
    b.mu.reg_write(A.UC_ARM_REG_R0, clip)
    b.mu.reg_write(A.UC_ARM_REG_R1, 0x11)
    b.mu.reg_write(A.UC_ARM_REG_R2, 0x12)
    b.mu.reg_write(A.UC_ARM_REG_R3, 0x13)
    b.mu.reg_write(A.UC_ARM_REG_SP, sp)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_grain"] | 1, bd.STOP, count=400)
    if "rb_pair" in bench.sym and leaf_v(bench) in (1, 5):
        # 3.1.R: value 1 is Rubber, 3.1.T: Fill; on a Slicer both play stock grains, unscaled
        res.check(b.u32(clip + 0xE0 + 0x48) == 0x40000000, "rubber leaves grain rate")
        res.check(b.u32(clip + 0xE0 + 0x60) == 0x12345678, "rubber leaves stretch pad")
    else:
        res.check(b.u32(clip + 0xE0 + 0x48) == 0x3F800000, "repitch scales grain rate")
        res.check(b.u32(clip + 0xE0 + 0x60) == 0, "repitch clears stretch pad")
    res.check(b.mu.reg_read(A.UC_ARM_REG_R0) == 2, "repitch keeps grain index")


SM_BASE = 0x38007000


def t_smooth_enum_labels(bench, res):
    b = bench.b
    if b.u32(REG_PTR) == 0:
        b.call(PARAM_REGISTER_ALL, 0)
    base = b.u32(REG_PTR)
    res.check(base != 0, "param registry allocated")
    slot = base + ID_REPITCH * 0x1C
    n = b.u32(slot + 0xC)
    labels = b.u32(slot + 8)
    # A bank-2 leaf (Bungee, or RB with WARP_LEAF=rb) adds a 7th mode; 3.1.E (clip-deck) predates it
    leaf = "Bungee" if "bg_mix_fw" in bench.sym else "RB" if "rb_mix_fw" in bench.sym else None
    res.check(n == (7 if leaf else 6), f"Warp enum count {n}")
    res.check(labels != 0, f"labels ptr {labels:#x}")
    if labels:
        res.check(b.cstr(b.u32(labels)) == "Stretch", "label0 Stretch")
        res.check(b.cstr(b.u32(labels + 4)) == "Repitch", "label1 Repitch")
        res.check(b.cstr(b.u32(labels + 8)) == "Smooth", "label2 Smooth")
        if "capi_mix" in bench.sym:                  # 3.1.Z still builds Snap here; only 3.1.E has Capi
            res.check(b.cstr(b.u32(labels + 12)) == "Capi", "label3 Capi")
        res.check(b.cstr(b.u32(labels + 16)) == "Vari", "label4 Vari")
        res.check(b.cstr(b.u32(labels + 20)) == "Grain", "label5 Grain")
        if leaf:
            res.check(b.cstr(b.u32(labels + 24)) == leaf, f"label6 {leaf}")


def t_smooth_clip_flag(bench, res):
    b = bench.b
    clip = 0x2001C800
    tempo = 0x2001CC00
    msg = 0x2001CD00
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(tempo, b"\x00" * 0x20)
    b.mu.mem_write(msg, b"\x00" * 0x18)
    # Unpacked key 0x100 -> col 0, row 1 -> packed key (row<<2)|col = 4
    b.w32(clip + 0x18, 0x100)
    b.w32(msg + 0xC, ID_REPITCH)
    b.w32(msg + 0x10, 2)
    b.call(bench.sym["state_init"])
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 2, f"clip +0xBB1 Smooth is {flag}")
    flags = 0x38005010 if "fm_init" in bench.sym else 0x38000010
    res.check(b.u8(flags + 4) == 2, f"SRAM4 flag {b.u8(flags + 4)}")


def t_smooth_main_rate_sets_reset(bench, res):
    b = bench.b
    clip = 0x2001C800
    grain = 0x2001D000
    A = bd.A
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(grain, b"\x00" * 0x80)
    b.mu.mem_write(SM_BASE, b"\x00" * 0x40)
    b.mu.mem_write(clip + 0xBB1, b"\x02")
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x3F800000)
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    res.check(r3 == grain, f"r3 clobbered to {r3:#x}")
    res.check(b.u32(grain + 0x48) == 0x3F800000, "Smooth leaves grain rate")
    res.check(b.u32(SM_BASE + 20) == 1, f"reset_req {b.u32(SM_BASE + 20)}")


def t_smooth_lane_hook(bench, res):
    res.check("hook_lane" in bench.sym, "hook_lane in cave")
    res.check("smooth_mix" in bench.sym, "smooth_mix in cave")
    res.check("smooth_process" in bench.sym, "smooth_process in cave")
    img = bench.b.image
    site = 0x080667B8
    off = site - bd.BASE
    res.check(img[off:off + 4].hex() != "2de9f04f", "per-lane site patched")


def t_capi_clip_flag(bench, res):
    b = bench.b
    clip = 0x2001C800
    tempo = 0x2001CC00
    msg = 0x2001CD00
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(tempo, b"\x00" * 0x20)
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(clip + 0x18, 0x100)
    b.w32(msg + 0xC, ID_REPITCH)
    b.w32(msg + 0x10, 3)
    b.call(bench.sym["state_init"])
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 3, f"clip +0xBB1 Capi is {flag}")
    flags = 0x38005010 if "fm_init" in bench.sym else 0x38000010
    res.check(b.u8(flags + 4) == 3, f"SRAM4 flag {b.u8(flags + 4)}")


def t_capi_main_rate_sets_reset(bench, res):
    b = bench.b
    clip = 0x2001C800
    grain = 0x2001D000
    A = bd.A
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(grain, b"\x00" * 0x80)
    b.mu.mem_write(SM_BASE, b"\x00" * 0x40)
    b.mu.mem_write(clip + 0xBB1, b"\x03")
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x3F800000)
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    res.check(r3 == grain, f"r3 clobbered to {r3:#x}")
    res.check(b.u32(grain + 0x48) == 0x3F800000, "Capi leaves grain rate")
    res.check(b.u32(SM_BASE + 20) == 1, f"reset_req {b.u32(SM_BASE + 20)}")


def t_capi_lane_hook(bench, res):
    res.check("capi_mix" in bench.sym, "capi_mix in cave")
    res.check("capi_process" in bench.sym, "capi_process in cave")
    res.check("capi_init" in bench.sym, "capi_init in cave")


def t_vari_clip_flag(bench, res):
    b = bench.b
    clip = 0x2001C800
    tempo = 0x2001CC00
    msg = 0x2001CD00
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(tempo, b"\x00" * 0x20)
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(clip + 0x18, 0x100)
    b.w32(msg + 0xC, ID_REPITCH)
    b.w32(msg + 0x10, 4)
    b.call(bench.sym["state_init"])
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 4, f"clip +0xBB1 Vari is {flag}")
    flags = 0x38005010 if "fm_init" in bench.sym else 0x38000010
    res.check(b.u8(flags + 4) == 4, f"SRAM4 flag {b.u8(flags + 4)}")


def t_vari_main_rate_sets_reset(bench, res):
    b = bench.b
    clip = 0x2001C800
    grain = 0x2001D000
    A = bd.A
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(grain, b"\x00" * 0x80)
    b.mu.mem_write(SM_BASE, b"\x00" * 0x40)
    b.mu.mem_write(clip + 0xBB1, b"\x04")
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x3F800000)
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    res.check(r3 == grain, f"r3 clobbered to {r3:#x}")
    res.check(b.u32(grain + 0x48) == 0x3F800000, "Vari leaves grain rate")
    res.check(b.u32(SM_BASE + 20) == 1, f"reset_req {b.u32(SM_BASE + 20)}")


def t_vari_lane_hook(bench, res):
    res.check("vari_mix" in bench.sym, "vari_mix in cave")
    res.check("vari_process" in bench.sym, "vari_process in cave")
    res.check("vari_init" in bench.sym, "vari_init in cave")
    res.check("vari_analyze" in bench.sym, "vari_analyze in cave")


def t_grain_clip_flag(bench, res):
    b = bench.b
    clip = 0x2001C800
    tempo = 0x2001CC00
    msg = 0x2001CD00
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(tempo, b"\x00" * 0x20)
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(clip + 0x18, 0x100)
    b.w32(msg + 0xC, ID_REPITCH)
    b.w32(msg + 0x10, 5)
    b.call(bench.sym["state_init"])
    b.call(0x08067648, clip, tempo, msg, 0)
    flag = b.u8(clip + 0xBB1)
    res.check(flag == 5, f"clip +0xBB1 Grain is {flag}")
    flags = 0x38005010 if "fm_init" in bench.sym else 0x38000010
    res.check(b.u8(flags + 4) == 5, f"SRAM4 flag {b.u8(flags + 4)}")


def t_grain_main_rate_rewrites_window(bench, res):
    """Warp Grain keeps stock rate and rewrites Clip_StartGrain #256 window to 2048."""
    b = bench.b
    clip = 0x2001C800
    grain = 0x2001D000
    A = bd.A
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.mu.mem_write(grain, b"\x00" * 0x80)
    b.mu.mem_write(SM_BASE, b"\x00" * 0x40)
    b.mu.mem_write(clip + 0xBB1, b"\x05")
    # Stock-like: window 256, end clamp at +0x58 = end - 256 (use end=10000).
    b.w32(grain + 0x54, 256)
    b.w32(grain + 0x5c, 256)
    b.w32(grain + 0x58, 10000 - 256)
    b.mu.reg_write(A.UC_ARM_REG_R3, grain)
    b.mu.reg_write(A.UC_ARM_REG_R4, clip)
    b.mu.reg_write(A.UC_ARM_REG_S15, 0x3F800000)
    b.mu.reg_write(A.UC_ARM_REG_SP, bd.BENCH_SP)
    b.mu.reg_write(A.UC_ARM_REG_LR, bd.STOP | 1)
    b.mu.emu_start(bench.sym["hook_main_rate"] | 1, bd.STOP, count=200)
    r3 = b.mu.reg_read(A.UC_ARM_REG_R3)
    res.check(r3 == grain, f"r3 clobbered to {r3:#x}")
    res.check(b.u32(grain + 0x48) == 0x3F800000, "Grain leaves grain rate")
    res.check(b.u32(grain + 0x54) == 2048, f"window +0x54 {b.u32(grain + 0x54)}")
    res.check(b.u32(grain + 0x5c) == 2048, f"window +0x5c {b.u32(grain + 0x5c)}")
    res.check(b.u32(grain + 0x58) == 10000 - 2048, f"end clamp +0x58 {b.u32(grain + 0x58)}")
    res.check(b.u32(SM_BASE + 20) == 0, f"Grain must not set SM_RESET ({b.u32(SM_BASE + 20)})")


def t_smooth_state_clears_owner(bench, res):
    """Power-on SRAM4 is random; state_init must clear SM_OWNER or Smooth/Capi never run."""
    b = bench.b
    b.mu.mem_write(SM_BASE, b"\xA5" * 0x40)
    b.call(bench.sym["state_init"])
    res.check(b.u32(SM_BASE) == 0x534D5448, f"magic {b.u32(SM_BASE):#x}")
    res.check(b.u32(SM_BASE + 4) == 0, f"owner still {b.u32(SM_BASE + 4):#x}")
    res.check(b.u32(SM_BASE + 24) == 0, f"block_done {b.u32(SM_BASE + 24)}")


# ---- patched-only tests
def t_enter_group(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    res.check(not bench.load_ok(), "no preset loaded for a group folder")
    res.check(not bench.screen, "stays on the preset screen")
    res.check(bench.browse() == "Presets\\_Test", f"browse path {bench.browse()!r}")
    rows = bench.rows()
    res.check(rows == bench.nested(["/Sub", "One", "Two"]), f"group rows {rows}")
    res.check(any(e[0] == 0x11 for e in bench.events), "list-done event posted (UI refresh)")


def t_nested_load_and_samples(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    bench.press_load("/Sub")
    res.check(bench.rows() == bench.nested(["Deep"]), f"nested rows {bench.rows()}")
    bench.press_load("Deep")
    res.check(len(bench.load_ok()) == 1, "Deep loads from Presets\\_Test\\Sub")
    res.check(bench.current() == "Deep", f"current preset {bench.current()!r}")
    res.check(bench.base() == "Presets\\_Test\\Sub", f"loaded-preset folder {bench.base()!r}")
    res.check(bench.join("Deep") == "Presets\\_Test\\Sub\\Deep", f"sample path base {bench.join('Deep')!r}")
    bench.up()
    bench.up()
    res.check(bench.browse() == "Presets", f"back at top {bench.browse()!r}")
    res.check(bench.join("Deep") == "Presets\\_Test\\Sub\\Deep",
              "sample path still uses the loaded preset's folder after browsing away")


def t_dotdot_floor(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    bench.up()
    res.check(bench.browse() == "Presets", f"'..' returns to {bench.browse()!r}")
    rows = bench.rows()
    res.check(".." not in rows, "no '..' row at the top level")
    res.check("Root" in rows, "root xml presets listed again at the top level")
    # a stray '..' command at the top must not climb above Presets
    bench.b.put_cstr(S_BROWSE, "Presets")
    bench.b.call(bench.sym["pop_comp"], S_BROWSE)
    res.check(bench.browse() == "Presets", "pop at the top stays at Presets")


def t_group_hides_root_xml(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    res.check("Root" not in bench.rows(), "root xml presets hidden inside a group")


def t_folder_with_preset_loads(bench, res):
    bench.refresh()
    bench.press_load("Beta")
    res.check(len(bench.load_ok()) == 1, "folder with preset.xml loads")
    res.check(bench.browse() == "Presets", "browse path unchanged")
    res.check(bench.base() == "Presets", f"loaded-preset folder {bench.base()!r}")


def t_boot_hook(bench, res):
    img = bench.b.image
    site = 0x0804418C - bd.BASE
    hw1, hw2 = int.from_bytes(img[site:site + 2], "little"), int.from_bytes(img[site + 2:site + 4], "little")
    res.check(hw1 >> 11 == 0x1E and hw2 >> 14 == 0x3, "main's first call is a bl (boot init)")
    bench.b.put_cstr(S_BROWSE, "Presets\\stale")
    bench.b.call(bench.sym["fm_init"])
    res.check(bench.browse() == "Presets" and bench.base() == "Presets", "boot init resets both paths")


def t_marker(bench, res):
    bench.refresh()
    rows = bench.rows()
    res.check("/_Test" in rows and "_Test" not in rows, "group folder marked with '/'")
    res.check("Beta" in rows and "Alpha" in rows, "preset folders unmarked")
    res.check("Root" in rows, "root xml preset unmarked")
    bench.press_load("/_Test")
    res.check(bench.rows() == bench.nested(["/Sub", "One", "Two"]), f"nested group marked {bench.rows()}")


def t_marker_cache(bench, res):
    """A second BuildList (the App_Update timer) must not touch the card again."""
    bench.refresh()
    first = bench.rows()
    reads = bench.b.reads
    bench.b.reads = 0
    bench.refresh()
    res.check(bench.rows() == first, f"cached rebuild rows {bench.rows()}")
    res.check(bench.b.reads < reads, f"cached rebuild reads {bench.b.reads} < first {reads}")
    res.check("/_Test" in bench.rows(), "marker still present after the cached rebuild")


def t_back(bench, res):
    bench.refresh()
    res.check(bench.back(), "BACK at the top level leaves the screen")
    bench.press_load("/_Test")
    bench.press_load("/Sub")
    res.check(not bench.back(), "BACK in a nested folder stays on the screen")
    res.check(bench.browse() == "Presets\\_Test", f"BACK goes up to {bench.browse()!r}")
    res.check(bench.rows() == bench.nested(["/Sub", "One", "Two"]), f"list rebuilt {bench.rows()}")
    res.check(not bench.back(), "BACK again stays on the screen")
    res.check(bench.browse() == "Presets", f"BACK reaches {bench.browse()!r}")
    res.check(bench.back(), "BACK at the top leaves the screen")
    res.check(bench.browse() == "Presets", "browse path stays at Presets")
    # Hardware BACK is HandleInput type 7. hook_type7 is a bl that can leave
    # that function's frame, so the bench only calls folder_back.
    tick = lambda: bench.b.w32(0x24015CE4, bench.b.u32(0x24015CE4) + 1000)
    bench.press_load("/_Test")
    tick()
    res.check(bench.b.call(bench.sym["folder_back"]) == 0, "folder_back pops a level")
    bench.pump()
    res.check(bench.browse() == "Presets", f"folder_back browse {bench.browse()!r}")
    tick()
    res.check(bench.b.call(bench.sym["folder_back"]) == 1, "folder_back at the top is a no-op")
    if not bench.dotdot:
        bench.press_load("/_Test")
        bench.press_load("/Sub")
        tick()
        bench.b.mu.mem_write(0x24020088 + 0x8CA4, b"\x02")    # Pads screen
        res.check(bench.b.call(bench.sym["folder_back"]) == 1, "BACK off the preset screen stays stock")
        bench.b.mu.mem_write(0x24020088 + 0x8CA4, b"\x07")
        bench.b.call(bench.sym["folder_back"])
        bench.b.call(bench.sym["folder_back"])                  # same press via the second hook
        bench.pump()
        res.check(bench.browse() == "Presets\\_Test", f"one press goes up one level ({bench.browse()!r})")
        tick()
        bench.b.call(bench.sym["folder_back"])
        bench.pump()
    bench.press_load("/_Test")
    bench.b.reads = 0
    bench.up()
    res.check(bench.browse() == "Presets", f"cached BACK browse {bench.browse()!r}")
    res.check(bench.b.reads < 100, f"cached BACK reads {bench.b.reads}")


def t_pset_up_button(bench, res):
    """The preset screen's leftmost button (id 0x7F) reads ^ below the top level."""
    b = bench.b
    scr = 0xC2000000
    b.mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    b.mu.mem_write(scr, b"\0" * 0x3000)
    b.call(0x080BE20D, scr)
    btn = scr + 0x630
    res.check(b.u32(btn + 0x14) == 0x7F and struct.unpack("<4i", b.mu.mem_read(btn + 4, 16)) == (0, 182, 38, 38),
              "the 38 x 38 id-0x7F button at the left of the top panel")
    texts = []
    b.stub(0x080BDE60, lambda board: 0)
    b.stub(0x080A3DEC, lambda board: (texts.append(board.cstr(board.arg(1))),
                                      board.mu.mem_write(board.arg(0) + 0x34 + 0x31, board.cstr(board.arg(1)).encode() + b"\0"))[1])
    bench.refresh()
    bench.press_load("/_Test")
    b.call(bench.sym["pset_on_event"], scr, EVT_BUF)
    b.call(bench.sym["pset_on_event"], scr, EVT_BUF)
    res.check(texts == ["^"], f"inside a folder: ^ (set once) {texts}")
    bench.back()
    b.call(bench.sym["pset_on_event"], scr, EVT_BUF)
    res.check(texts == ["^", ""], f"back at the top: blank {texts}")
    res.check(".." not in bench.rows(), "no .. row")
    b.unstub(0x080BDE60)
    b.unstub(0x080A3DEC)


def diff(before, after):
    return sorted(after - before), sorted(before - after)


def t_save_as_into_group(bench, res):
    bench.refresh()
    bench.press_load("Beta")
    bench.press_load("/_Test")
    before = bench.files()
    bench.save_as("Copy")
    added, removed = diff(before, bench.files())
    want = ["Presets/_Test/Copy", "Presets/_Test/Copy/kick.wav", "Presets/_Test/Copy/preset.als",
            "Presets/_Test/Copy/preset.xml"]
    res.check(added == want, f"Save As writes into the browsed folder {added}")
    res.check(removed == [], f"nothing removed {removed}")
    res.check(mkcard.read(bench.card_path, "Presets/_Test/Copy/kick.wav")
              == mkcard.read(bench.card_path, "Presets/Beta/kick.wav"), "sample copied from the loaded preset")
    res.check(bench.current() == "Copy", f"saved preset is loaded {bench.current()!r}")
    res.check(bench.base() == "Presets\\_Test", f"loaded-preset folder {bench.base()!r}")
    res.check("Copy" in bench.rows(), "saved preset listed in the browsed folder")


def t_save_as_same_name(bench, res):
    bench.refresh()
    bench.press_load("Beta")
    bench.press_load("/_Test")
    before = bench.files()
    kick = mkcard.read(bench.card_path, "Presets/Beta/kick.wav")
    bench.save_as("Beta")
    added, removed = diff(before, bench.files())
    want = ["Presets/_Test/Beta", "Presets/_Test/Beta/kick.wav", "Presets/_Test/Beta/preset.als",
            "Presets/_Test/Beta/preset.xml"]
    res.check(added == want, f"same-name Save As into another folder {added}")
    res.check(removed == [], f"nothing removed {removed}")
    res.check(mkcard.read(bench.card_path, "Presets/Beta/kick.wav") == kick, "original sample untouched")
    res.check(mkcard.read(bench.card_path, "Presets/_Test/Beta/kick.wav") == kick, "sample copied")
    res.check(bench.base() == "Presets\\_Test", f"loaded-preset folder {bench.base()!r}")


def t_save_as_top_level(bench, res):
    bench.refresh()
    bench.press_load("Beta")
    before = bench.files()
    bench.save_as("Gamma")
    added, removed = diff(before, bench.files())
    want = ["Presets/Gamma", "Presets/Gamma/kick.wav", "Presets/Gamma/preset.als", "Presets/Gamma/preset.xml"]
    res.check(added == want, f"top-level Save As as stock {added}")
    res.check(removed == [], f"nothing removed {removed}")
    res.check(bench.current() == "Gamma" and bench.base() == "Presets", "saved preset loaded at the top")


def t_save_while_browsing(bench, res):
    bench.refresh()
    bench.press_load("Beta")
    bench.press_load("/_Test")
    before = bench.files()
    xml = mkcard.read(bench.card_path, "Presets/Beta/preset.xml")
    bench.b.stub(SESSION_COPY, lambda b: 0)
    bench.events.clear()
    bench.b.call(REQUEST_SAVE, PM, 0, 1)
    bench.pump()
    after = bench.files()
    added, removed = diff(before, after)
    res.check(added == ["Presets/Beta/preset.als"] and removed == [],
              f"Save writes the loaded preset's folder {added} {removed}")
    res.check(not any(f.startswith("Presets/_Test/Beta") for f in after), "nothing written to the browsed folder")
    res.check(mkcard.read(bench.card_path, "Presets/Beta/preset.xml") != b"" and xml != b"", "preset.xml rewritten")


DECOY_TREE = dict(TREE)
DECOY_TREE.update({
    "Presets/One/preset.xml": mkcard.PRESET_XML,     # same name as Presets/_Test/One
    "Presets/One/keep.wav": "RIFF\x24\0\0\0WAVEfmt ",
    "Presets/_Test/One/junk.wav": "RIFF\x24\0\0\0WAVEfmt ",
    "One.xml": mkcard.PRESET_XML,                    # legacy root presets with row names
    "Two.xml": mkcard.PRESET_XML,
})


def t_delete(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    bench.press_load("/_Test")
    before = bench.files()
    bench.request(REQUEST_DELETE, "One")
    after = bench.files()
    added, removed = diff(before, after)
    res.check(removed == ["Presets/_Test/One", "Presets/_Test/One/junk.wav", "Presets/_Test/One/preset.xml"],
              f"Delete removes only the browsed preset {removed}")
    res.check(added == [], "nothing added")
    res.check("One" not in bench.rows(), "row removed from the list")
    for name in ("..", "/Sub", "Sub"):
        bench.request(REQUEST_DELETE, name)
        now = bench.files()
        res.check(now == after, f"Delete {name!r} is refused")
        res.check(any(e[0] == 0x27 for e in bench.events), f"Delete {name!r} still completes for the UI")
    bench.up()
    bench.request(REQUEST_DELETE, "/_Test")
    res.check(bench.files() == after, "Delete on a top-level group row is refused")


def t_rename(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    bench.press_load("/_Test")
    before = bench.files()
    bench.request(REQUEST_RENAME, "Two", "Three")
    after = bench.files()
    added, removed = diff(before, after)
    res.check(added == ["Presets/_Test/Three", "Presets/_Test/Three/preset.xml"], f"renamed in the browsed folder {added}")
    res.check(removed == ["Presets/_Test/Two", "Presets/_Test/Two/preset.xml"], f"old name gone {removed}")
    res.check("Two.xml" in after, "legacy root Two.xml untouched")
    bench.request(REQUEST_RENAME, "/Sub", "/Sub2")
    now = bench.files()
    res.check("Presets/_Test/Sub2/Deep/preset.xml" in now and "Presets/_Test/Sub" not in now, "folder renamed")
    res.check(bench.rows() == bench.nested(["/Sub2", "One", "Three"]), f"list rebuilt with the marker {bench.rows()}")
    bench.request(REQUEST_RENAME, "..", "X")
    res.check(bench.files() == now, "rename of '..' refused")
    bench.request(REQUEST_RENAME, "One", "..")
    res.check(bench.files() == now, "rename to '..' refused")


def t_new(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    bench.press_load("/_Test")
    before = bench.files()
    bench.request(REQUEST_NEW, "Fresh")
    added, removed = diff(before, bench.files())
    res.check(added == ["Presets/_Test/Fresh", "Presets/_Test/Fresh/preset.xml"], f"new preset in the browsed folder {added}")
    res.check(removed == [], "nothing removed")


def t_clean(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    bench.press_load("/_Test")
    before = bench.files()
    for name in ("/Sub", "Sub", ".."):
        bench.request(REQUEST_CLEAN, name)
        res.check(bench.files() == before, f"Clean {name!r} is refused")
    bench.request(REQUEST_CLEAN, "One")
    added, removed = diff(before, bench.files())
    res.check(added == [], "nothing added")
    res.check(all(r.startswith("Presets/_Test/One/") for r in removed), f"Clean stays in the browsed preset {removed}")
    res.check("Presets/One/keep.wav" in bench.files(), "top-level decoy untouched")


# ---- folder marks on a big group folder (3.1.Z+: one f_stat per row, kept per folder, idle steps)
def _big_name(i):
    return f"A rather long subfolder name number {i:03d} for testing"


BIG_TREE = dict(TREE)
for _i in range(88):                                   # odd: group folders, even: presets
    BIG_TREE[f"Presets/_G00/{_big_name(_i)}/" + ("Inner/preset.xml" if _i % 2 else "preset.xml")] = mkcard.PRESET_XML
for _g in range(1, 9):
    BIG_TREE[f"Presets/_G{_g:02d}/Child/preset.xml"] = mkcard.PRESET_XML


def _reads(bench, fn):
    """Sector reads of fn, and the reads of each idle dispatcher step it caused."""
    bench.b.reads = 0
    bench.idle_reads.clear()
    fn()
    return bench.b.reads, list(bench.idle_reads)


def t_mark_big_folder(bench, res):
    """Entering a folder of 88 long-named subfolders. Before 2026-10-21 that was two f_stats per row in
    one command: 5,542 sector reads on this card with sample streaming stopped, and the same again
    on every re-entry (only the parent folder's marks were kept)."""
    bench.refresh()
    total, steps = _reads(bench, lambda: bench.press_load("/_G00"))
    first = total - sum(steps)
    rows = bench.rows()
    want = sorted(("/" if i % 2 else "") + _big_name(i) for i in range(88))
    res.check(sorted(r for r in rows if r != "..") == want, f"all 88 rows, 44 marked ({sum(r.startswith('/') for r in rows)})")
    res.check(first <= 400, f"the Load press reads {first} sectors (was 5,542)")
    res.check(steps and max(steps) <= 300, f"the rest in {len(steps)} idle steps of <= {max(steps or [0])} reads")
    res.check(total <= 3300, f"whole first visit {total} reads (one f_stat per row)")
    bench.up()
    again, steps = _reads(bench, lambda: bench.press_load("/_G00"))
    res.check(not steps and again <= 250, f"re-entering reads {again} (was 5,541), no checks")
    res.check(bench.rows() == rows, "same marks from the cache")
    for g in range(1, 7):                              # Presets, _G00 and these six: 8 slots
        bench.up()
        bench.press_load(f"/_G{g:02d}")
    bench.up()
    again, steps = _reads(bench, lambda: bench.press_load("/_G00"))
    res.check(not steps and again <= 250, f"still cached after six other folders ({again} reads)")
    for g in range(1, 9):                              # eight more: _G00 is the oldest, reused
        bench.up()
        bench.press_load(f"/_G{g:02d}")
    bench.up()
    again, steps = _reads(bench, lambda: bench.press_load("/_G00"))
    res.check(steps, f"least recently used folder was dropped ({len(steps)} idle steps)")
    res.check(bench.rows() == rows, "marks rebuilt the same")


def t_mark_lost_delete(bench, res):
    """Delete and Clean ask the card, never the marks: losing them cannot delete a group tree."""
    bench.refresh()
    bench.press_load("Alpha")
    bench.press_load("/_Test")
    before = bench.files()
    bench.b.mu.mem_write(STATE + 0x720, b"\0" * 0x4000)   # every folder's marks gone
    bench.b.w32(STATE + 0x14, 0)
    for name in ("Sub", "/Sub"):
        bench.request(REQUEST_DELETE, name)
        res.check(bench.files() == before, f"Delete {name!r} refused without marks")
        bench.request(REQUEST_CLEAN, name)
        res.check(bench.files() == before, f"Clean {name!r} refused without marks")


# ---- saving (3.1.Z+): exFAT, deep and long paths, failures shown
WAV_3M = "RIFF\x24\0\0\0WAVEfmt " + "\0" * (3 << 20)
DEEP = "Presets/_Test/Sub/Deeper Group Folder/Even Deeper"
SAVE_TREE = dict(TREE)
SAVE_TREE.update({
    "Presets/Beta/kick.wav": WAV_3M,
    "Presets/Beta/Sub Samples/snare.wav": WAV_3M[:200000],
    f"{DEEP}/Leaf/preset.xml": mkcard.PRESET_XML,
})
SAVE_FILES = ["kick.wav", "Sub Samples", "Sub Samples/snare.wav", "preset.als", "preset.xml"]
# A browse path of 209 characters: "Presets\" + three long group names.
G1 = "_Long group folder name number one and more words here to make it long"[:70]
G2 = "Another long nested group folder name two with many more words to pad it out"[:76]
G3 = "Third level group with a long name and yet more words"[:60]
LONG_TREE = dict(SAVE_TREE)
LONG_TREE[f"Presets/{G1}/{G2}/{G3}/Leaf/preset.xml"] = mkcard.PRESET_XML
LONG_TREE["Presets/_Test/Copy"] = "not a folder"           # Save As "Copy" cannot write here


def _save_into(bench, path, new):
    bench.refresh()
    while bench.browse() != "Presets":
        bench.up()
    bench.press_load("Beta")
    for p in path:
        bench.press_load(p)
    before = bench.files()
    bench.save_as(new)
    added, removed = diff(before, bench.files())
    return added, removed


def _failed_save(bench, res, what, added, removed):
    res.check(added == [] and removed == [], f"{what}: nothing written {added} {removed}")
    res.check((0x26, 0, 1) in bench.events, f"{what}: the preset screen shows the error ({bench.events})")
    res.check((3, 0, 1) in bench.events and bench.current() == "Beta",
              f"{what}: the Load after it keeps the session ({bench.current()!r})")


def t_save_as_exfat(bench, res):
    res.check(mkcard.is_exfat(bench.card_path), "exFAT card, 32 KB clusters")
    added, removed = _save_into(bench, ["/_Test"], "Copy")
    res.check(added == sorted(["Presets/_Test/Copy"] + [f"Presets/_Test/Copy/{f}" for f in SAVE_FILES]),
              f"Save As into a group folder {added}")
    res.check(mkcard.read(bench.card_path, "Presets/_Test/Copy/kick.wav") == WAV_3M.encode("latin1"),
              "3 MB sample copied intact")
    added, removed = _save_into(bench, ["/_Test", "/Sub", "/Deeper Group Folder", "/Even Deeper"], "Deep Copy")
    res.check(added == sorted([f"{DEEP}/Deep Copy"] + [f"{DEEP}/Deep Copy/{f}" for f in SAVE_FILES]),
              f"Save As four groups deep {added}")
    res.check(bench.current() == "Deep Copy" and bench.base() == DEEP.replace("/", "\\"), "loaded from there")


def t_save_as_long_path(bench, res):
    """<browse>\\name\\preset.xml past 255: stock cut the paths, made the folder, then overran
    path_join's stack buffer on the first sample (a reset on the unit)."""
    path = ["/" + G1, "/" + G2, "/" + G3]
    added, removed = _save_into(bench, path, "My new preset name that is quite long too")
    _failed_save(bench, res, "xml path of 261", added, removed)
    # preset.xml fits (247), "Sub Samples\\snare.wav" does not (259): the copy stops there
    added, removed = _save_into(bench, path, "My new preset name that i")
    dest = f"Presets/{G1}/{G2}/{G3}/My new preset name that i"
    res.check(f"{dest}/preset.xml" in added, "preset.xml written")
    res.check(all(a == dest or a[len(dest) + 1:] in SAVE_FILES for a in added), f"no cut file names {added}")
    res.check((0x26, 0, 1) in bench.events, "sample copy error shown")


def t_save_as_onto_group(bench, res):
    """A Save As name that is a group folder would put preset.xml in it and hide its presets."""
    for name in ("Sub", "/Sub"):
        added, removed = _save_into(bench, ["/_Test"], name)
        _failed_save(bench, res, f"Save As {name!r} onto group Sub", added, removed)
    # the keyboard and unique_name side (UI task)
    b = bench.b
    texts = []
    b.stub(0x080C42AC, lambda bb: texts.append(bb.cstr(bb.arg(1))) or 0)
    buf = b.put_cstr(NAME_BUF, "/Sub")
    b.call(bench.sym["hook_kb_saveas"], 0x2001E800, buf)
    b.put_cstr(NAME_BUF, "One")
    b.call(bench.sym["hook_kb_saveas"], 0x2001E800, NAME_BUF)
    b.unstub(0x080C42AC)
    res.check(texts == ["Beta", "One"], f"Save As pre-fill: loaded preset on a group row {texts}")
    b.call(0x08091244, PATH_BUF, PM, b.put_cstr(NAME_BUF, "sub"))
    res.check(b.cstr(PATH_BUF).lower() != "sub", f"unique_name numbers a group's name ({b.cstr(PATH_BUF)!r})")


def t_save_fail_shown(bench, res):
    """doc_save failing only logged: Save As left a folder without preset.xml and the Load after it
    fell back to the first row that loads."""
    added, removed = _save_into(bench, ["/_Test"], "Copy")      # "Copy" is a file there
    _failed_save(bench, res, "Save As where preset.xml cannot be created", added, removed)
    res.check(not any(a.startswith("Presets/_Test/Copy/") for a in added), "no partial folder")
    bench.b.stub(0x08096754, lambda bb: 0)                      # doc_save fails
    bench.b.stub(SESSION_COPY, lambda bb: 0)
    bench.events.clear()
    bench.b.call(REQUEST_SAVE, PM, 0, 1)
    bench.pump()
    bench.b.unstub(SESSION_COPY)
    bench.b.unstub(0x08096754)
    res.check((0x26, 0, 1) in bench.events, f"failed Save shows the error {bench.events}")


def t_pathjoin_bounded(bench, res):
    b = bench.b
    pj = 0x080431A4
    for base, comp, want in (("Presets", "X", "Presets\\X"), ("Presets\\", "X", "Presets\\X"),
                             ("Presets", "\\X", "Presets\\X"), ("", "X", "\\X")):
        b.call(pj, PATH_BUF, 0, b.put_cstr(NAME_BUF, base), b.put_cstr(NAME2_BUF, comp))
        res.check(b.cstr(PATH_BUF) == want, f"path_join({base!r}, {comp!r}) = {b.cstr(PATH_BUF)!r}")
    base, comp = "P" * 250, "c" * 200
    b.mu.mem_write(PATH_BUF, b"\xee" * 0x300)
    b.call(pj, PATH_BUF, 0, b.put_cstr(NAME_BUF, base), b.put_cstr(NAME2_BUF, comp))
    res.check(b.cstr(PATH_BUF, 400) == (base + "\\" + comp)[:255], "450 characters cut at 255, no overrun")
    res.check(bytes(b.mu.mem_read(PATH_BUF + 256, 16)) == b"\xee" * 16, "nothing written past dest[255]")


CELL_CTOR = 0x08093C7C
CELL_ADD = 0x08093CCA
SLICE_LIST = 0x2001C800
SESSION_PTR = PM + 700  # PresetMgr +0x2BC, 4th arg to doc_open


def _slice_xml(n_tags=128, extra=None):
    tags = "\n".join(f'<slice pos="{i * 1000}"/>' for i in range(n_tags))
    ext = ""
    if extra:
        ext = f' ext="{",".join(str(p) for p in extra)}"'
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            "<document>\n<session version=\"2\">\n"
            '<cell row="2" column="0" layer="0" filename="kick.wav" type="sample">\n'
            f'<slices{ext}>\n{tags}\n'
            "</slices>\n</cell>\n</session>\n</document>\n")


SLICE_EXTRA = [128000 + i * 1000 for i in range(72)]
SLICE_TREE = dict(TREE)
SLICE_TREE["Presets/Alpha/preset.xml"] = _slice_xml(128, SLICE_EXTRA)
SLICE_TREE["Presets/Alpha/kick.wav"] = "RIFF\x24\0\0\0WAVEfmt "


def _cell_count(bench, session=None, row=2, col=0):
    if session is None:
        session = bench.b.u32(SESSION_PTR)
    off = row * 0xF0 + col * 0x30
    return bench.b.u32(session + off + 0x2C), bench.b.u32(session + off + 0x28), session


def t_slice_hooks(bench, res):
    res.check(menu_letter(bench.b.image) in "ZSE", "menu letter is Z (S faster Smooth, E Clip regions)")
    for name in ("hook_slices_max", "hook_slice_max", "hook_engine_cap",
                 "hook_cell_cap", "hook_save_iter", "hook_save_bag", "hook_cell_ext",
                 "hook_pad_slice", "hook_pad_done"):
        res.check(name in bench.sym, f"{name} in cave")
    img = bench.b.image
    # stock immediates must be gone
    res.check(img[0x0808D32C - bd.BASE:0x0808D32C - bd.BASE + 2] != bytes.fromhex("8020"),
              "Slices max site patched")
    res.check(img[0x08068B62 - bd.BASE:0x08068B62 - bd.BASE + 2] != bytes.fromhex("7f2a"),
              "engine insert cap patched")


def t_slice_cell_grow(bench, res):
    b = bench.b
    if "sl_init" in bench.sym:
        b.call(bench.sym["sl_init"])
    b.w32(SLICE_LIST, 0)
    b.w32(SLICE_LIST + 4, 0)
    b.call(CELL_CTOR, SLICE_LIST)
    for i in range(200):
        b.call(CELL_ADD, SLICE_LIST, i * 100)
    count = b.u32(SLICE_LIST + 4)
    ptr = b.u32(SLICE_LIST)
    res.check(count == 200, f"grown count {count}")
    res.check(0x30000000 <= ptr < 0x30040000, f"table in SRAM1–2 {ptr:#x}")
    res.check(b.u32(ptr + 199 * 4) == 19900, "last extra position")
    res.check(b.u32(ptr) == 0, "first position")


def t_slice_stock_cap(bench, res):
    b = bench.b
    b.w32(SLICE_LIST, 0)
    b.w32(SLICE_LIST + 4, 0)
    b.call(CELL_CTOR, SLICE_LIST)
    ptr0 = b.u32(SLICE_LIST)
    for i in range(200):
        b.call(CELL_ADD, SLICE_LIST, i * 100)
    count = b.u32(SLICE_LIST + 4)
    ptr = b.u32(SLICE_LIST)
    res.check(count == 128, f"stock count {count}")
    res.check(ptr == ptr0, "stock table not moved")


def t_slice_xml_load(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    res.check(len(bench.load_ok()) == 1, "Alpha with ext loaded")
    count, ptr, session = _cell_count(bench)
    res.check(session != 0, f"session at {session:#x}")
    res.check(count == 200, f"restored count {count}")
    res.check(0x30000000 <= ptr < 0x30040000, f"grown on load {ptr:#x}")
    res.check(bench.b.u32(ptr) == 0, "first tag stays first")
    res.check(bench.b.u32(ptr + 127 * 4) == 127000, "last tag stays at 127")
    res.check(bench.b.u32(ptr + 128 * 4) == SLICE_EXTRA[0], "first ext position")
    res.check(bench.b.u32(ptr + 199 * 4) == SLICE_EXTRA[-1], "last ext position")
    other, _, _ = _cell_count(bench, session, row=0, col=0)
    res.check(other == 0, f"row 0 did not receive the slices ({other})")


def t_slice_stock_xml(bench, res):
    bench.refresh()
    bench.press_load("Alpha")
    res.check(len(bench.load_ok()) == 1, "stock loaded the ext preset")
    count, ptr, session = _cell_count(bench)
    res.check(session != 0, f"session at {session:#x}")
    res.check(count == 128, f"stock kept 128 tags, count {count}")
    if ptr:
        res.check(bench.b.u32(ptr) == 0, "first tag")
        res.check(bench.b.u32(ptr + 127 * 4) == 127000, "last of 128 tags")
# ---- Mix overhaul tests (3.1.Q)

STOCK_IMAGE = str(Path(__file__).resolve().parents[2] / "firmware" / "bins" / "3.1.9" / "BLACKBOX.bin")
MIX_OBJ = 0x30010000          # bench-built Mix widget (SRAM1 is unused by 3.1.X)
MIX_EV = 0x30014000
MIX_FL = 0x30014100
MX_STATE = 0x38005200
BTN_MIX = 0x24009B36
BTN_INFO = 0x24009BDE
UWTICK = 0x24015CE4
APP_OBJ = 0x24020088
SET_SCREEN = 0x0809EAEC
SLIDER_STEP = 0x080B0D54
GET_PARAM = 0x08099504
MIX_FADERS = {"tl": 0x2C48, "tr": 0x2DA0, "bl": 0x2CF4, "br": 0x2E4C}
MIX_FRAMES = (0x29C8, 0x2A68, 0x2B08, 0x2BA8)
MIX_VALUES = {0x04: -6000, 0x3E: 700, 0xC8: 400, 0x5C: 0, 0x62: 300, 0x3C: 0, 0xD9: 250, 0xDA: 900}
MIX_MAIN = {"tl": 0x04, "bl": 0x3E, "tr": 0xC8, "br": 0x5C}
MIX_HELD = {"tl": 0x62, "bl": 0x3C, "tr": 0xD9, "br": 0xDA}


def _mix_setup(bench, build=True):
    b = bench.b
    b.mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    b.mu.mem_write(MX_STATE, b"\0" * 16)
    b.mu.mem_write(BTN_MIX, b"\x01")
    b.mu.mem_write(BTN_INFO, b"\x01")

    def getp(board):
        board.mu.mem_write(board.arg(3), struct.pack("<i", MIX_VALUES.get(board.arg(2), 0)))
        return 1
    b.stub(GET_PARAM, getp)
    b.mu.mem_write(MIX_OBJ, b"\0" * 0x3000)
    if build:
        if b.call(0x0808E305, 0x3E) == 0:   # param registry not built yet
            b.call(0x0808C159)
        b.call(0x080B5F98, MIX_OBJ)
    return b


def _mix_ev(b, typ, idx=0, val=0, key=0):
    b.mu.mem_write(MIX_EV, b"\0" * 0x18)
    b.mu.mem_write(MIX_EV, struct.pack("<H", typ))
    b.mu.mem_write(MIX_EV + 8, struct.pack("<H", key))
    b.mu.mem_write(MIX_EV + 0xC, struct.pack("<H", idx))
    b.mu.mem_write(MIX_EV + 0x10, struct.pack("<h", val))
    return MIX_EV


def _mix_tick(bench):
    b = bench.b
    b.mu.mem_write(MIX_FL, b"\0\0")
    b.call(bench.sym["mix_tick"], MIX_OBJ, MIX_FL)
    return b.mu.mem_read(MIX_FL, 2)


def _fader(b, name):
    f = MIX_OBJ + MIX_FADERS[name]
    return {"rect": struct.unpack("<4i", b.mu.mem_read(f + 4, 16)), "hidden": b.u8(f + 0x30),
            "id": struct.unpack("<H", b.mu.mem_read(f + 0x3E, 2))[0],
            "pad": struct.unpack("<H", b.mu.mem_read(f + 0x38, 2))[0], "parent": b.u32(f + 0x2C)}


def t_mix_hooks(bench, res):
    img = bench.b.image
    for va, name, stock in ((0x080F0F10, "mix_tick", 0x080B6601),
                            (0x080F0F20, "mix_touch_down", 0x080B5F45),
                            (0x080F0F24, "mix_touch_move", 0x080B5EBD),
                            (0x080F0F40, "mix_child_event", 0x080B5A4D),
                            (0x080F0F44, "mix_on_event", 0x080B5C71)):
        word = struct.unpack_from("<I", img, va - bd.BASE)[0]
        res.check(word == bench.sym[name] | 1, f"Mix vtable {va:#x} -> {name} ({word:#x}, stock {stock:#x})")
    res.check(menu_letter(img) in ("Q", "R", "B", "T"), f"menu letter is {menu_letter(img)}")


def t_mix_pads(bench, res):
    """Mix pads: MIX held selects, Mute toggles (stock mute touch), Solo; INFO cycles the mode."""
    b = _mix_setup(bench)
    cells = [MIX_OBJ + 0x3AC + i * 0x1A0 for i in range(16)]
    for i, c in enumerate(cells):
        b.mu.mem_write(c + 0x38, struct.pack("<H", (i % 4) | (i // 4) << 4))
        b.mu.mem_write(c + 0x189, b"\x01")
    posts, calls, hit = [], [], [None]

    def post(board):
        m = board.arg(1)
        posts.append((board.u16(m), board.u16(m + 8), board.u32(m + 12)))
    b.stub(UI_POST_UP, post)

    def hitf(board):
        if hit[0] is None:
            return 0
        board.w32(board.arg(2), hit[0])
        return 1
    b.stub(0x080B5E78, hitf)
    b.stub(0x080B5F44, lambda board: calls.append("mute_down") and None)
    b.stub(0x080B5EBC, lambda board: calls.append("mute_move") and None)
    b.call(bench.sym["mix_set_mode"], MIX_OBJ, 0)
    res.check(b.u8(MIX_OBJ + 0x1E40) == 1 and b.u32(MX_STATE + 96) == MIX_OBJ, "Mix always shows mute colours")
    pt = MIX_EV + 0x40
    down = lambda i: (hit.__setitem__(0, cells[i]), b.call(bench.sym["mix_touch_down"], MIX_OBJ, pt, 0))
    b.w32(MX_STATE + 112, 1)                                   # MIX held
    down(6)
    res.check(posts == [(0x63, 0x12, 0)] and not calls, f"MIX held: tap selects pad 0x12, no mute {posts} {calls}")
    b.w32(MX_STATE + 112, 0)
    posts.clear()
    down(6)
    res.check(calls == ["mute_down"] and not posts, f"Mute mode: stock mute touch {calls}")
    b.call(bench.sym["mix_touch_move"], MIX_OBJ, pt, 0)
    res.check(calls[-1] == "mute_move", "Mute mode: drag paints mutes")
    b.mu.mem_write(cells[2] + 0x189, b"\x00")                # a hand mute on pad 2
    b.call(bench.sym["mix_info"], APP_OBJ, 0x2F, 0, 0)
    res.check(b.u32(MX_STATE + 92) == 1, "INFO: Solo")
    calls.clear()
    down(5)
    muted = sorted(p[1] for p in posts if p[0] == 0x44 and p[2] == 0)
    want = sorted(((i % 4) | (i // 4) << 4) for i in range(16) if i not in (2, 5))
    res.check(muted == want and not calls and b.u32(MX_STATE + 100) == 1 << 5,
              f"Solo pad 5: every other pad muted {muted}")
    res.check(all(b.u8(cells[i] + 0x189) == (1 if i == 5 else 0) for i in range(16)), "cells show the solo")
    posts.clear()
    down(6)
    res.check(posts == [(0x44, 0x12, 1)] and b.u32(MX_STATE + 100) == (1 << 5 | 1 << 6), f"add pad 6 to the solo {posts}")
    b.call(bench.sym["mix_touch_move"], MIX_OBJ, pt, 0)
    res.check(not calls, "Solo mode: no drag painting")
    down(5)
    posts.clear()
    down(6)
    res.check(b.u32(MX_STATE + 100) == 0 and all(b.u8(cells[i] + 0x189) == (0 if i == 2 else 1) for i in range(16)),
              "empty solo set: the hand mute on pad 2 comes back, the rest play")
    fills, outlines = [], []
    b.stub(0x0808EA22, lambda board: fills.append(board.arg(1)) and None)
    b.stub(0x0808E994, lambda board: outlines.append(board.arg(1)) and None)
    down(9)
    b.call(bench.sym["mix_mute_fill"], cells[9] + 4, 9, 0x30018000)
    b.call(bench.sym["mix_mute_fill"], cells[3] + 4, 2, 0x30018000)
    res.check(fills == [0x1C, 2] and outlines == [0x14] * 4, f"Solo colours: soloed yellow, all outlined {fills} {outlines}")
    b.call(bench.sym["mix_info"], APP_OBJ, 0x2F, 0, 0)
    res.check(b.u32(MX_STATE + 92) == 0, "INFO: back to Mute")
    fills.clear(); outlines.clear()
    b.call(bench.sym["mix_mute_fill"], cells[9] + 4, 9, 0x30018000)
    res.check(fills == [9] and not outlines, f"Mute mode: stock colours {fills}")
    calls.clear()
    down(4)
    res.check(calls == ["mute_down"] and b.u32(MX_STATE + 100) == 0, "a hand edit in Mute drops the solo set")


def t_mix_knobs(bench, res):
    b = _mix_setup(bench, build=False)
    steps = []
    b.stub(SLIDER_STEP, lambda board: steps.append((board.arg(0) - MIX_OBJ,
                                                    struct.unpack("<i", struct.pack("<I", board.arg(1)))[0])))
    for idx in range(5):
        b.call(bench.sym["mix_on_event"], MIX_OBJ, _mix_ev(b, 0x32, idx=idx, val=3 - idx))
    want = [(0x1E48, 3), (0x2128, 2), (0x2408, 1), (0x26E8, 0)]
    res.check(steps == want, f"knobs TL/BL/TR/BR -> corner sliders {[(hex(o), d) for o, d in steps]}")


def _slider_ids(b):
    return {n: struct.unpack("<I", b.mu.mem_read(MIX_OBJ + off + 0x14, 4))[0]
            for n, off in (("tl", 0x1E48), ("bl", 0x2128), ("tr", 0x2408), ("br", 0x26E8))}


def t_mix_layout(bench, res):
    b = _mix_setup(bench)
    fl = _mix_tick(bench)
    res.check(fl[0] & 1, "first tick asks for a full redraw")
    res.check(all(_fader(b, n)["parent"] == MIX_OBJ for n in MIX_FADERS), "faders attached to Mix")
    res.check(all(b.u32(MIX_OBJ + off) == bench.sym["fader_vt"] for off in MIX_FADERS.values()),
              "faders use the Mix fader vtable")
    res.check(all(b.u8(MIX_OBJ + f + 0x30) == 1 for f in MIX_FRAMES), "empty frames hidden")
    rects = {n: _fader(b, n)["rect"] for n in MIX_FADERS}
    res.check(rects == {"tl": (1, 111, 30, 109), "tr": (288, 111, 30, 109), "bl": (1, 1, 30, 109),
                        "br": (288, 1, 30, 109)}, f"corner rects {rects}")
    ids = {n: _fader(b, n)["id"] for n in MIX_FADERS}
    res.check(ids == MIX_MAIN, f"main layer Vol/Decay/Cutoff/Pitch {ids}")
    res.check(_slider_ids(b) == MIX_MAIN, f"knobs bound to the main layer {_slider_ids(b)}")
    _mix_tick(bench)
    res.check(_mix_tick(bench)[0] == 0, "attach only once")
    b.mu.mem_write(BTN_MIX, b"\x00")
    fl = _mix_tick(bench)
    ids = {n: _fader(b, n)["id"] for n in MIX_FADERS}
    res.check(fl[0] & 1 and ids == MIX_HELD, f"MIX held: Pan/Attack/SendA/SendB {ids}")
    res.check(fl[1] & 1, "layer change also asks for the partial pass (fader labels repaint there)")
    res.check(_slider_ids(b) == MIX_HELD, f"knobs follow the MIX layer {_slider_ids(b)}")
    res.check(not any(_fader(b, n)["hidden"] for n in MIX_FADERS), "all four faders shown in both layers")
    b.w32(UWTICK, b.u32(UWTICK) + 1000)            # a hold, not a short press
    b.mu.mem_write(BTN_MIX, b"\x01")
    _mix_tick(bench)
    res.check({n: _fader(b, n)["id"] for n in MIX_FADERS} == MIX_MAIN, "MIX released after a hold: main layer back")
    b.call(bench.sym["mix_on_event"], MIX_OBJ, _mix_ev(b, 0x63, key=0x23))
    pads = {n: _fader(b, n)["pad"] for n in MIX_FADERS}
    res.check(set(pads.values()) == {0x23}, f"faders follow the selected pad {pads}")
    res.check(_slider_ids(b) == MIX_MAIN, f"select keeps the layer's knob params {_slider_ids(b)}")


def _touch_setup(bench):
    b = _mix_setup(bench)
    _mix_tick(bench)
    b.call(bench.sym["mix_on_event"], MIX_OBJ, _mix_ev(b, 0x63, key=0x23))
    posts = []
    b.stub(UI_POST_UP, lambda board: posts.append(struct.unpack("<HxxxxxxHxxIi", board.mu.mem_read(board.arg(1), 0x14))))
    return b, posts


def _touch(bench, name, y, hold_ms, t0=10000):
    b = bench.b
    f = MIX_OBJ + MIX_FADERS[name]
    vt = bench.sym["fader_vt"]
    pt = MIX_EV + 0x40
    b.mu.mem_write(pt, struct.pack("<2i", 10, y))
    b.w32(UWTICK, t0)
    b.call(b.u32(vt + 0x10), f, pt)          # touchDown (through the vtable, as the GUI does)
    b.w32(UWTICK, t0 + hold_ms)
    b.call(b.u32(vt + 0x18), f)              # touchUp


def t_mix_fader_touch(bench, res):
    b, posts = _touch_setup(bench)
    _touch(bench, "tl", 111 + 109 // 2, 50)      # Vol at -6 dB: not at rest, so no blip
    ok = len(posts) == 1 and posts[0][:3] == (0x6E, 0x23, 0x04)
    res.check(ok, f"dragging Vol posts 0x6E for the selected pad {posts}")
    res.check(ok and -60000 < posts[0][3] < -30000, f"mid-height Vol is mid-range ({posts[0][3] if posts else None})")


def t_mix_blip(bench, res):
    b, posts = _touch_setup(bench)
    MIX_VALUES[0x5C] = 0
    _touch(bench, "br", 1 + 80, 120)             # Pitch at 0, quick tap
    res.check(len(posts) == 2 and posts[0][2] == 0x5C and posts[0][3] != 0 and posts[1][2:] == (0x5C, 0),
              f"quick tap from rest blips Pitch and puts 0 back {posts}")
    posts.clear()
    _touch(bench, "br", 1 + 80, 600)             # long hold keeps the value
    res.check(len(posts) == 1 and posts[0][3] != 0, f"long hold from rest keeps the value {posts}")
    posts.clear()
    MIX_VALUES[0x04] = -96000
    _touch(bench, "tl", 111 + 30, 100)           # Vol at minimum
    res.check(len(posts) == 2 and posts[1][2:] == (0x04, -96000), f"Vol blips back to minimum {posts}")
    MIX_VALUES[0x04] = -6000
    posts.clear()
    _touch(bench, "tl", 111 + 30, 100)           # Vol not at rest
    res.check(len(posts) == 1, f"no blip when not at rest {posts}")
    posts.clear()
    b.mu.mem_write(BTN_MIX, b"\x00")
    _mix_tick(bench)
    MIX_VALUES[0x62] = 0
    _touch(bench, "tl", 111 + 30, 100)           # Pan at centre: never blips
    res.check(len(posts) == 1 and posts[0][2] == 0x62, f"Pan at centre does not blip {posts}")
    MIX_VALUES[0x62] = 300


def _draw_rects(bench, name):
    """Run the Mix fader draw with a full-redraw context; return (colour, rect) fills."""
    b = bench.b
    fills = []
    b.stub(0x0808EA22, lambda board: fills.append((board.arg(1), struct.unpack("<4i", board.mu.mem_read(board.arg(0), 16)))))
    b.stub(0x0808E994, lambda board: None)
    ctx = MIX_EV + 0x80
    b.mu.mem_write(ctx, struct.pack("<BBHIi", 1, 1, 0, 0x30016000, 0))
    b.call(bench.sym["fader_draw"], MIX_OBJ + MIX_FADERS[name], ctx)
    b.unstub(0x0808EA22)
    b.unstub(0x0808E994)
    return fills


def t_mix_detent_draw(bench, res):
    b = _mix_setup(bench)
    MIX_VALUES[0xC8] = -500
    _mix_tick(bench)
    fills = _draw_rects(bench, "tr")                 # Cutoff, logical rect (288,111,30,109)
    centre = 111 + 109 // 2
    res.check((3, (288, centre, 30, 1)) in fills, f"Cutoff draws a white centre line {fills}")
    bars = [r for c, r in fills if c == 5]
    res.check(len(bars) == 1 and bars[0][1] + bars[0][3] == centre and bars[0][3] > 20,
              f"negative Cutoff bar hangs below the centre {bars}")
    MIX_VALUES[0xC8] = 400
    b.call(bench.sym["mix_on_event"], MIX_OBJ, _mix_ev(b, 0x66, key=0))
    bars = [r for c, r in _draw_rects(bench, "tr") if c == 5]
    res.check(len(bars) == 1 and bars[0][1] == centre and bars[0][3] > 15, f"positive Cutoff bar rises from the centre {bars}")
    fills = _draw_rects(bench, "tl")                 # Vol: -96..+12 dB is not bipolar
    res.check(not any(c == 3 for c, _ in fills), f"Vol keeps the stock look {fills}")


def t_mix_detent_snap(bench, res):
    b, posts = _touch_setup(bench)
    MIX_VALUES[0xC8] = 400                          # not at rest: no blip
    centre = 111 + 109 // 2
    _touch(bench, "tr", centre + 3, 600)
    res.check(posts and posts[-1][2:] == (0xC8, 0), f"3 px off centre snaps Cutoff to 0 {posts}")
    posts.clear()
    _touch(bench, "tr", centre + 10, 600)
    res.check(posts and posts[-1][3] > 0, f"10 px off centre does not snap {posts}")
    posts.clear()
    _touch(bench, "tl", centre + 2, 600)
    res.check(posts and posts[-1][2] == 0x04 and posts[-1][3] != 0, f"Vol never snaps {posts}")


def t_nav_knobs(bench, res):
    """Batch 0: Pads/Seq encoders 0-2 and the scroll-list row knob no longer navigate."""
    b = _mix_setup(bench, build=False)
    moved = []
    b.stub(0x080B0B74, lambda board: moved.append(board.arg(0)))    # slider accumulate
    b.stub(0x080AED14, lambda board: None)
    obj = 0x30020000
    for name, ctor in (("Pads", 0x080B4528), ("Seq", 0x080B4F34)):
        b.mu.mem_write(obj, b"\0" * 0x6000)
        b.call(ctor, obj)
        onev = b.u32(b.u32(obj) + 0x34)
        res.check(onev == bench.sym[name.lower() + "_on_event"] | 1, f"{name} onEvent wrapped")
        for idx in range(4):
            if name == "Seq" and idx == 2:
                continue                                         # length knob (t_seq_length)
            moved.clear()
            b.call(onev, obj, _mix_ev(b, 0x32, idx=idx, val=1))
            if idx < 3:
                res.check(not moved, f"{name} encoder {idx} no longer navigates {[hex(m - obj) for m in moved]}")
            else:
                res.check(not moved, f"{name} encoder 3 no longer switches the side panel")
    img, stock = bench.b.image, open(STOCK_IMAGE, "rb").read()
    for va, what in ((0x080B9BA4, "row select"), (0x080B9A3C, "value change")):
        res.check(img[va - bd.BASE: va - bd.BASE + 0x40] == stock[va - bd.BASE: va - bd.BASE + 0x40],
                  f"parameter-menu encoders stay stock ({what}, {va:#x})")


def _btn(b, idx, pressed):
    b.mu.mem_write(0x24009AB0 + idx * 0x18 + 0xE, b"\x00" if pressed else b"\x01")


def t_held_select(bench, res):
    b = _mix_setup(bench, build=False)
    posts = []
    b.stub(UI_POST_UP, lambda board: posts.append(struct.unpack("<HxxxxxxH", board.mu.mem_read(board.arg(1), 10))))
    obj = 0x30020000
    for name, idx, typ, key_out in (("pads", 0, 0x63, 0x23), ("seq", 2, 0xFC, 0x123)):
        fn = bench.sym[name + "_child_event"]
        _btn(b, idx, True)
        posts.clear()
        r = b.call(fn, obj, _mix_ev(b, 3, key=0x23))
        res.check(r == 0 and posts == [(typ, key_out)], f"{name.upper()} held: press only selects {posts}")
        posts.clear()
        r = b.call(fn, obj, _mix_ev(b, 4, key=0x23))
        res.check(r == 0 and not posts, f"{name.upper()} held: release swallowed")
        _btn(b, idx, False)
    posts.clear()
    r = b.call(bench.sym["pads_child_event"], obj, _mix_ev(b, 3, key=0x23))
    res.check(r == 1 and not posts, "PADS not held: pad press propagates (plays)")
    posts.clear()
    r = b.call(bench.sym["seq_child_event"], obj, _mix_ev(b, 3, key=0x23))
    res.check(r == 0 and posts == [(0xFA, 0x123)], f"SEQS not held: stock play toggle {posts}")


SEQ_OBJ, P_GET, P_SET = 0x08097CE8, 0x08093E9C, 0x08093ECC
EVL_GET, EVL_DEL, SEQ_DOUBLE, SEQ_PUSH, SEQ_SNAP = 0x08063D08, 0x08063C08, 0x0809C5B4, 0x080985A0, 0x08098254


def _seq_stubs(b, params, events):
    log = {"set": [], "del": [], "push": [], "double": [], "snap": 0, "text": []}
    base, layer_obj = 0x30030000, 0x30031000
    b.stub(SEQ_OBJ, lambda board: base if board.arg(2) == 0 else layer_obj)
    b.stub(P_GET, lambda board: params.get(board.arg(1), 0) & 0xFFFFFFFF)
    b.stub(P_SET, lambda board: log["set"].append((board.arg(1), board.arg(2))))

    def get(board):
        i = board.arg(1)
        if i >= len(events):
            return 0
        pos, handle = events[i]
        board.mu.mem_write(board.arg(2), struct.pack("<6I", 0, pos, 0, handle, 0, 0))
        return 1
    b.stub(EVL_GET, get)
    b.stub(EVL_DEL, lambda board: log["del"].append(board.arg(1)))
    b.stub(SEQ_PUSH, lambda board: log["push"].append(board.arg(2)))
    b.stub(SEQ_DOUBLE, lambda board: log["double"].append(board.u8(board.arg(1) + 8)))
    b.stub(SEQ_SNAP, lambda board: log.__setitem__("snap", log["snap"] + 1))
    for fn in (0x08094A1C, 0x08094BB2, 0x08063CC0, 0x080B5758, 0x0809C2C8):
        b.stub(fn, lambda board: 0)
    b.stub(0x080A3DEC, lambda board: log["text"].append(board.cstr(board.arg(1))))
    return log


def _knob_world(bench, n, step_idx=10, rec_len=0, events=(), step_mode=0, dbl=1):
    """_SeqWorld plus a live layer: App_SetParam 0x85/0x86 land in the layer, events in a list."""
    w = _SeqWorld(bench, rec_len=rec_len, step_idx=step_idx, glob=dbl)
    b = w.b
    w.la[0x86] = n
    w.l0[0x88] = step_mode
    w.events = [list(e) for e in events]
    w.snap = []
    w.ops = []
    b.w32(LA_OBJ + 0x1C, len(w.events))

    def setp(board):
        ident, val = board.arg(2), struct.unpack("<i", struct.pack("<I", board.arg(3)))[0]
        w.sets.append((ident, val))
        w.vals[ident] = val
        if ident in (0x85, 0x86):
            w.la[ident] = val
    b.stub(APP_SETP, setp)
    b.stub(0x08098254, lambda board: w.ops.append("snap") and None)
    b.stub(0x08094A1C, lambda board: None)
    b.stub(0x08063CC0, lambda board: None)
    b.stub(0x08094BB2, lambda board: w.snap.__setitem__(slice(None), [list(e) for e in w.events]) and None)

    def evl_get(board):
        i = board.arg(1)
        if i >= len(w.snap):
            return 0
        board.mu.mem_write(board.arg(2), struct.pack("<6I", *w.snap[i]))
        return 1
    b.stub(0x08063D08, evl_get)

    def evl_add(board):
        w.events.append(list(struct.unpack("<6I", board.mu.mem_read(board.arg(1), 24))))
        board.w32(LA_OBJ + 0x1C, len(w.events))
        return 1
    b.stub(0x08063B28, evl_add)
    b.stub(0x080985A0, lambda board: w.ops.append(("push", board.arg(2))) and None)
    b.stub(0x080B5758, lambda board: w.ops.append(("post", board.u16(board.arg(1)))) and None)
    b.stub(0x0809C2C8, lambda board: None)
    return w


def t_seq_length(bench, res):
    """Top-right knob on Seq: stock list speed, presets step, other lengths x2 / /2, labels."""
    seq = 0x30020000

    def turn(w, *deltas, key=0x23):
        b = w.b
        b.mu.mem_write(seq, b"\0" * 0x4000)
        b.mu.mem_write(seq + 0x1A90, bytes([key]))
        w.labels = []
        b.stub(0x080A3DEC, lambda board: w.labels.append(board.cstr(board.arg(1))))
        for d in deltas:
            b.call(bench.sym["seq_on_event"], seq, _mix_ev(b, 0x32, idx=2, val=d))

    w = _knob_world(bench, 16, rec_len=4)                     # 1 bar at 1/16
    turn(w, 399)
    res.check(w.sets == [] and w.labels == ["1 bar"], f"399 units do nothing {w.sets} {w.labels}")
    turn(w, 1)
    res.check(w.sets == [(0x86, 32), (SR_LEN, 5), (SR_SET, 1)] and w.labels[-1] == "2 bar",
              f"400 units: 1 bar -> 2 bar {w.sets} {w.labels}")
    w.sets.clear()
    turn(w, -200, -199)
    res.check(w.sets == [], f"-399 back does nothing {w.sets}")
    turn(w, -1)
    res.check(w.sets == [(0x86, 16), (SR_LEN, 4), (SR_SET, 1)], f"-400: 2 bar -> 1 bar {w.sets}")
    w.sets.clear()
    turn(w, 800)
    res.check(w.sets[-3:] == [(0x86, 64), (SR_LEN, 6), (SR_SET, 1)] and len(w.sets) == 6, f"800 units = two steps {w.sets}")
    w.sets.clear()
    turn(w, 300)
    turn(w, 300, key=0x24)
    res.check(w.sets == [], f"another sequence restarts the accumulator {w.sets}")

    w = _knob_world(bench, 512, rec_len=9)
    turn(w, 400)
    res.check(w.sets == [], f"32 bars is the top {w.sets}")
    w = _knob_world(bench, 2, rec_len=1)
    turn(w, -400)
    res.check(w.sets == [(SR_LEN, 0), (SR_SET, 1)], f"1/8 down: Custom, length kept {w.sets}")
    w = _knob_world(bench, 16, rec_len=0)
    turn(w, -400)
    res.check(w.sets == [] and w.labels[-1] == "Cust", f"empty Custom is the bottom {w.sets}")
    turn(w, 400)
    res.check(w.sets == [(SR_LEN, 1), (SR_SET, 1)], f"empty Custom up: 1/8 {w.sets}")

    lens = lambda w: [x for x in w.sets if x[0] in (0x85, 0x86)]
    w = _knob_world(bench, 15, rec_len=0, events=[(1, 0, 240, 1, 60, 0)])   # recorded 15/16
    turn(w, 0)
    res.check(w.labels == ["15/16"], f"15 x 1/16 shows 15/16 {w.labels}")
    turn(w, 400)
    res.check(w.sets == [(0x86, 30), (SR_LEN, 10)] and w.labels[-1] == "30/16" and bench.b.cstr(SR_TXT) == "30/16",
              f"x2: 30/16, Rec Len = own length {w.sets} {w.labels}")
    turn(w, -400)
    res.check(lens(w)[-1] == (0x86, 15), f"/2: 15/16 {w.sets}")
    turn(w, -400)
    res.check(lens(w)[-1] == (0x85, 12) and w.labels[-1] == "15/32", f"/2 odd: 15/32 {w.sets} {w.labels}")
    turn(w, -400)
    res.check(lens(w)[-1] == (0x85, 14) and w.labels[-1] == "15/64", f"/2 again: 15/64 {w.sets}")
    turn(w, 400)
    turn(w, 400)
    res.check(lens(w)[-2:] == [(0x85, 12), (0x85, 10)] and w.labels[-1] == "15/16", f"x2 goes back to the user's 1/16 first {w.sets} {w.labels}")
    turn(w, 400)
    res.check(lens(w)[-1] == (0x86, 30), f"then doubles the count: 30/16 {w.sets}")
    w = _knob_world(bench, 15, step_idx=14, rec_len=10, events=[(1, 0, 240, 1, 60, 0)])
    turn(w, -400)
    res.check(w.sets == [(SR_LEN, 0), (SR_SET, 1)], f"15/64 down: nothing shorter, so Custom {w.sets}")
    w = _knob_world(bench, 8, rec_len=10, events=[(1, 0, 240, 1, 60, 0)])
    turn(w, 400)
    res.check(w.sets == [(0x86, 16), (SR_LEN, 4), (SR_SET, 1)], f"8/16 counts as the 1/2 preset: x2 -> 1 bar {w.sets}")
    w = _knob_world(bench, 300, rec_len=10, events=[(1, 0, 240, 1, 60, 0)])
    turn(w, 400)
    res.check(lens(w) == [(0x85, 8)], f"x2 past 512 steps: 2x longer step {w.sets}")

    for n, si, rl, ev, want in ((63, 10, 0, 1, "63/16"), (48, 10, 0, 1, "48/16"), (100, 10, 0, 1, "100st"),
                                (7, 5, 0, 1, "7st"), (12, 5, 0, 1, "4 bar"), (15, 10, 0, 0, "Cust"),
                                (16, 10, 4, 0, "1 bar"), (16, 10, 0, 1, "1 bar"),
                                (2, 10, 1, 0, "1/8"), (512, 10, 9, 0, "32bar")):
        w = _knob_world(bench, n, step_idx=si, rec_len=rl, events=[(1, 0, 240, 1, 60, 0)] * ev)
        w.labels = []
        w.b.stub(0x080A3DEC, lambda board: w.labels.append(board.cstr(board.arg(1))))
        w.b.call(bench.sym["seq_bar_label"], seq)
        res.check(w.labels == [want], f"{n} steps idx {si} -> {w.labels} (want {want})")

    notes = [(1, 0, 240, 7, 60 | 100 << 16, 0), (1, 1920, 240, 8, 64 | 100 << 16, 0)]
    for mode, unit in ((0, 240), (1, 960)):
        w = _knob_world(bench, 16, rec_len=4, events=notes, step_mode=mode)
        turn(w, 400)
        added = w.events[2:]
        res.check([e[1] for e in added] == [16 * unit, 1920 + 16 * unit] and all(e[3] == 0 for e in added)
                  and [e[4] for e in added] == [e[4] for e in notes],
                  f"Dbl Copy (step mode {mode}) copies the notes one old length later {added}")
        res.check(w.ops[0] == "snap" and ("push", 1) in w.ops and ("post", 0x66) in w.ops,
                  f"undo snapshot, push, refresh {w.ops}")
    w = _knob_world(bench, 16, rec_len=4, events=notes, dbl=0)
    turn(w, 400)
    res.check(len(w.events) == 2 and w.sets[0] == (0x86, 32) and "snap" not in w.ops, f"Dbl Copy Off only lengthens {w.sets}")
    w = _knob_world(bench, 32, rec_len=5, events=notes + [(1, 5000, 240, 9, 67, 0)])
    turn(w, -400)
    res.check(len(w.events) == 3 and w.sets[0] == (0x86, 16), f"halving never deletes {w.sets}")


def t_eq_untouched_defaults(bench, res):
    for pristine, want in ((True, [(0x11C, 2), (0x121, 3), (0x126, 3), (0x12B, 4)]), (False, [])):
        w = _SeqWorld(bench)
        for n in range(4):
            w.vals[0x118 + 5 * n] = 0 if pristine or n else 300       # band 1 gain moved when not pristine
            w.vals[0x119 + 5 * n] = 200 * (n + 1)
            w.vals[0x11C + 5 * n] = 0
        page = 0x30020000
        w.b.mu.mem_write(page, b"\0" * 0x2000)
        w.b.mu.mem_write(page + 0x1BCC, struct.pack("<H", 0x320))
        w.b.call(bench.sym["eq_fix_defaults"], page)
        res.check(w.sets == want, f"{'untouched' if pristine else 'edited'} EQ -> {w.sets}")

def t_eq_tap(bench, res):
    b = _mix_setup(bench, build=False)
    graph = 0x30020000
    b.mu.mem_write(graph, b"\0" * 0x2000)
    for i, (x, y) in enumerate(((40, 60), (110, 120), (180, 80), (250, 150))):
        b.mu.mem_write(graph + 0x19D0 + i * 0x34 + 4, struct.pack("<4i", x - 5, y - 5, 10, 10))
    b.mu.mem_write(graph + 0x1B98, struct.pack("<H", 0x320))
    posts, downs = [], []
    b.stub(UI_POST_UP, lambda board: posts.append(struct.unpack("<HxxxxxxHxxIi", board.mu.mem_read(board.arg(1), 0x14))))
    b.stub(0x080A9B40, lambda board: downs.append(board.u32(board.arg(1))))
    pt = MIX_EV + 0x40
    b.mu.mem_write(pt, struct.pack("<2i", 176, 84))
    b.call(bench.sym["eq_touch_down"], graph, pt)
    res.check(b.u32(graph + 0x1B44) == 2 and b.u8(graph + 0x19D0 + 2 * 0x34 + 0x31) == 1, "tap selects the nearest dot (band 3)")
    res.check(posts == [(0x6E, 0x320, 0x12E, 2)], f"posts eqactband = 2 {posts}")
    res.check(downs == [176], "then stock touchDown drags it")
    posts.clear()
    b.call(bench.sym["eq_touch_down"], graph, pt)
    res.check(not posts, "tapping the active band posts nothing")
    img = bench.b.image
    got = [img[a - bd.BASE: a - bd.BASE + 2].hex() for a in (0x0809470E, 0x0809474E, 0x0809478E, 0x080947CE)]
    res.check(got == ["0222", "0322", "0322", "0422"], f"default bands L Shelf / Param / Param / H Shelf {got}")


def t_fx_return(bench, res):
    b = _mix_setup(bench, build=False)
    screens = []
    b.stub(SET_SCREEN, lambda board: screens.append(board.arg(1)))
    b.mu.mem_write(APP_OBJ + 0x28, struct.pack("<H", 0x37))
    b.call(bench.sym["fx_to_return"], APP_OBJ, 0x30, 0, 0)
    res.check(screens == [0x23] and b.u16(APP_OBJ + 0x28) == 0x300, f"FX from DJ FX opens Return A {screens}")
    page = 0x30020000
    b.mu.mem_write(page, b"\0" * 0x3800)
    b.call(0x080ABA84, page)
    b.stub(0x080AB900, lambda board: None)
    b.call(bench.sym["fxret_on_event"], page, _mix_ev(b, 0x66))
    btns = [page + 0x70 + 2 * 0xA0, page + 0x70 + 0xA0, page + 0x70, page + 0x3AC]   # left to right
    ids = [b.u32(btn + 0x14) for btn in btns]
    sel = [b.u8(btn + 0x96) for btn in btns]
    xs = [struct.unpack("<4i", b.mu.mem_read(btn + 4, 16)) for btn in btns]
    res.check(ids == [0x7A0, 0x7A1, 0x7A2, 0x7A3] and sel == [1, 0, 0, 0], f"buttons A Delay / B Reverb / EQ / empty, A lit {ids} {sel}")
    lineup = "dj_tools_tick" in bench.sym       # 3.1.T: A Dly | B Rvb | EQ | Master, 80 px each
    edges = [0, 80, 160, 240] if lineup else [0, 104, 216, 268]
    res.check([r[1] for r in xs] == [0, 0, 0, 0] and [r[0] for r in xs] == edges, f"row along the bottom edge {xs}")
    res.check(struct.unpack("<4i", b.mu.mem_read(page + 0x34 + 4, 16))[1] == 0, "button bar moved to the bottom")
    screens.clear()
    ev = _mix_ev(b, 1)
    b.w32(ev + 0xC, 0x7A1)
    r = b.call(bench.sym["fxret_child_event"], page, ev)
    res.check(r == 0 and screens == [0x23] and b.u16(APP_OBJ + 0x28) == 0x310, f"B shows Reverb {screens}")
    screens.clear()
    b.w32(ev + 0xC, 0x7A2)
    b.call(bench.sym["fxret_child_event"], page, ev)
    res.check(screens == [0x36], f"EQ opens the EQ page {screens}")
    screens.clear()
    b.w32(ev + 0xC, 0x7A3)
    r = b.call(bench.sym["fxret_child_event"], page, ev)
    if lineup:
        res.check(r == 0 and screens == [0x29], f"Master opens Tools page 4 {screens}")
    else:
        res.check(r == 0 and not screens, "the empty button does nothing")
    b.mu.mem_write(APP_OBJ + 0x28, struct.pack("<H", 0x37))
    screens.clear()
    b.call(bench.sym["fx_to_return"], APP_OBJ, 0x30, 0, 0)
    res.check(b.u16(APP_OBJ + 0x28) == 0x310, "FX returns to the last return shown (B)")


def t_mix_enter_press(bench, res):
    """The MIX press that opens Mix is not a hold; the second layer needs a fresh press."""
    b = _mix_setup(bench)
    b.stub(SET_SCREEN, lambda board: None)
    b.mu.mem_write(BTN_MIX, b"\x00")
    b.call(bench.sym["mix_btn_screen"], APP_OBJ, 0x2E, 0, 0)    # MIX pressed on Pads
    _mix_tick(bench)
    res.check({n: _fader(b, n)["id"] for n in MIX_FADERS} == MIX_MAIN, "entering press shows the main layer")
    b.mu.mem_write(BTN_MIX, b"\x01")
    _mix_tick(bench)
    b.mu.mem_write(BTN_MIX, b"\x00")
    _mix_tick(bench)
    res.check({n: _fader(b, n)["id"] for n in MIX_FADERS} == MIX_HELD, "a new press on Mix shows the MIX layer")
    b.call(bench.sym["mix_on_event"], MIX_OBJ, _mix_ev(b, 0x66, key=0))
    fl = _mix_tick(bench)
    res.check(fl[0] & 1 and fl[1] & 1, f"a value refresh asks for both redraw passes {fl.hex()}")


def t_seq_panel(bench, res):
    b = _mix_setup(bench, build=False)
    seq = 0x30020000
    b.mu.mem_write(seq, b"\0" * 0x6000)
    b.call(0x080B4F34, seq)
    b.stub(0x080B55F4, lambda board: None)
    hid = lambda: (b.u8(seq + 0x3D5C + 0x30), b.u8(seq + 0x4010 + 0x30))
    fl = MIX_FL
    res.check(hid() == (0, 1), f"stock shows OFF/UNDO/CLR {hid()}")
    _btn(b, 2, True)
    b.mu.mem_write(fl, b"\0\0")
    b.call(bench.sym["seq_tick"], seq, fl)
    res.check(hid() == (1, 0) and b.mu.mem_read(fl, 2) == b"\1\1", f"SEQS held shows the layer panel A-D {hid()}")
    _btn(b, 2, False)
    b.call(bench.sym["seq_tick"], seq, fl)
    res.check(hid() == (0, 1), f"release brings OFF/UNDO/CLR back {hid()}")


def t_pads_panel(bench, res):
    b = _mix_setup(bench, build=False)
    pads = 0x30020000
    b.mu.mem_write(pads, b"\0" * 0x3000)
    b.call(0x080B4528, pads)
    b.stub(0x080B476C, lambda board: None)
    hid = lambda: (b.u8(pads + 0x2110 + 0x30), b.u8(pads + 0x21F0 + 0x30))
    fl = MIX_FL
    res.check(hid() == (0, 1), f"stock shows the VEL fader {hid()}")
    _btn(b, 0, True)
    b.mu.mem_write(fl, b"\0\0")
    b.call(bench.sym["pads_tick"], pads, fl)
    res.check(hid() == (1, 0) and b.mu.mem_read(fl, 2) == b"\1\1", f"PADS held shows CUT/COPY/PSTE/CLR {hid()}")
    _btn(b, 0, False)
    b.call(bench.sym["pads_tick"], pads, fl)
    res.check(hid() == (0, 1), f"release brings VEL back {hid()}")


# Functions using double-precision VFP that Unicorn's M7 can't execute (EQ curve maths, the EQ
# graph draw): stubbed for the real-GUI flow. Pure maths/drawing, no layout or event state.
GUI_DP_STUBS = (0x080602B0, 0x08060538, 0x080606C0, 0x08060880, 0x08060930, 0x08060A28,
                0x080A9900, 0x080AAC28, 0x080AB130)
GUI_DRAW_STUBS = (0x08041C68, 0x08041EC0, 0x08041E5C, 0x08041E8E, 0x08041BA4, 0x08041BF2,
                  0x08041BE8, 0x08041C1C, 0x0808F7E8, 0x080451DC, 0x080451EC, 0x08043DEA)


def t_gui_fx_flow(bench, res):
    """Real GUI + App: FX/Return/EQ navigation with the draw loop, no faults, no unaligned words.

    Unicorn doesn't fault on an unaligned LDM (the M7 does), so every unaligned word access is
    recorded; 3.1.Q once hard-faulted on FX -> Return that way."""
    from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
    from unicorn import arm_const as A
    b = bench.b
    mu = b.mu
    mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    b.unstub(SET_SCREEN)
    b.stub(0x080A2258, lambda board: 0)            # App_Update: the bench preset state resets screens
    tick = [0]

    def gettick(board):
        tick[0] += 5
        board.w32(UWTICK, tick[0])
        return tick[0]
    b.stub(0x0807794C, gettick)
    for fn in GUI_DP_STUBS + GUI_DRAW_STUBS:
        b.stub(fn, lambda board: 0)
    unaligned = {}

    def watch(mu_, access, addr, size, value, data):
        if size >= 4 and addr & 3:
            pc = mu_.reg_read(A.UC_ARM_REG_PC)
            unaligned[pc] = unaligned.get(pc, 0) + 1
    mu.hook_add(UC_HOOK_MEM_READ, watch)
    mu.hook_add(UC_HOOK_MEM_WRITE, watch)
    gui = 0xC2000000
    mu.mem_write(gui, b"\0" * 0x100000)
    b.call(0x0808BE4C, gui)
    b.w32(0x24002E6C, gui)
    for off, typ in ((0x2FC0, 0x26), (0x2FD8, 0x4B), (0x2FF0, 0x36), (0x3008, 0)):
        mu.mem_write(0x2001C800, bytes([typ]))
        b.call(0x08094A1C, APP_OBJ + off, 0x2001C800)
    ev = 0x2001C600

    def pump():
        for _ in range(3):
            b.call(0x0808B654, gui)
            b.call(0x0808B2D0, gui, limit=50_000_000)

    def fx():
        mu.mem_write(ev, b"\0" * 0x40)
        mu.mem_write(ev, struct.pack("<H", 0xF9))
        b.w32(ev + 0xC, 4)
        b.call(0x080A2E60, APP_OBJ, ev)

    def tap(fn, ident):
        mu.mem_write(ev, b"\0" * 0x40)
        mu.mem_write(ev, struct.pack("<H", 1))
        b.w32(ev + 0xC, ident)
        b.call(bench.sym[fn], 0, ev)

    def where():
        return b.u8(APP_OBJ + 0x8CA4), b.u32(gui + 0x280 + 0x34) - gui
    mu.mem_write(APP_OBJ + 0x8CA4, b"\x02")
    mu.mem_write(APP_OBJ + 0x8CC3, b"\x00")
    b.call(SET_SCREEN, APP_OBJ, 2, 0, 0)
    pump()
    steps = [("FX", None, (0x37, 0x905A0)), ("FX", None, (0x23, 0x36910)),
             ("fxret_child_event", 0x7A2, (0x36, 0x8DE50)), ("eq_child_event", 0x7A1, (0x23, 0x36910)),
             ("fxret_child_event", 0x7A2, (0x36, 0x8DE50)), ("eq_child_event", 0x7A2, (0x36, 0x8DE50)),
             ("FX", None, (0x37, 0x905A0))]
    for what, ident, want in steps:
        if what == "FX":
            fx()
        else:
            tap(what, ident)
        pump()
        res.check(where() == want, f"{what} {ident and hex(ident) or ''} -> screen/page {[hex(x) for x in where()]}")
    eq = gui + 0x8DE50
    res.check(struct.unpack("<4i", mu.mem_read(eq + 0x34 + 4, 16))[1] == 40, "EQ graph starts above the row")
    res.check(all(b.u32(0x38005400 + i * 0xA0 + 0x2C) == eq for i in range(4)), "EQ row buttons attached to the EQ page")
    res.check(not unaligned, f"no unaligned word accesses {[(hex(k), v) for k, v in unaligned.items()][:5]}")


# ---- Custom sequence recording (seqrec.S)

SR_LEN, SR_DEF, SR_SET = 0x19D, 0x19E, 0x19F
SR_TXT = 0x38005700
APP_SETP, GETP = 0x0809FA50, 0x08099504
SEQ_OBJ_FN, P_GET_FN = 0x08097CE8, 0x08093E9C
L0_OBJ, LA_OBJ = 0x30030000, 0x30031000
SE_OBJ = 0x30032000


class _SeqWorld:
    """Stubs for the stock param/sequence calls seqrec makes; records App_SetParam calls."""

    def __init__(self, bench, rec_len=0, touched=0, events=0, playing=0, step_idx=10, quant_idx=1, glob=1):
        b = self.b = bench.b
        self.sets = []
        self.vals = {SR_LEN: rec_len, SR_SET: touched}
        self.l0 = {0xBE: 0, 0xC0: 1, 0x46: quant_idx}       # active layer B
        self.la = {0x85: step_idx}
        self.glob = glob
        self.ticks = 0
        self.calls = []
        b.mu.mem_write(LA_OBJ, b"\0" * 0x40)
        b.w32(LA_OBJ + 0x1C, events)
        b.w32(0x2400A9C0 + 0x9A74, SE_OBJ)
        b.mu.mem_write(SE_OBJ, b"\0" * 0x40)
        b.mu.mem_write(SE_OBJ + 0x38, bytes([playing]))

        def setp(board):
            ident, val = board.arg(2), struct.unpack("<i", struct.pack("<I", board.arg(3)))[0]
            self.sets.append((ident, val))
            self.vals[ident] = val
        b.stub(APP_SETP, setp)

        def getp(board):
            board.w32(board.arg(3), self.vals.get(board.arg(2), 0))
            return 1
        b.stub(GETP, getp)
        b.stub(SEQ_OBJ_FN, lambda board: L0_OBJ if board.arg(2) == 0 else LA_OBJ)

        def pget(board):
            obj, ident = board.arg(0), board.arg(1)
            if obj == 0x24028D18:
                return self.glob
            return (self.l0 if obj == L0_OBJ else self.la).get(ident, 0)
        b.stub(P_GET_FN, pget)

        def ticks(board):
            board.mu.mem_write(board.arg(0), struct.pack("<q", self.ticks))
        b.stub(0x0804CA68, ticks)
        def ac60(board):
            on = board.u32(board.arg(1) + 0xC)
            self.calls.append(("ac60", on, list(self.sets)))
            board.mu.mem_write(APP_OBJ + 0x8CC1, b"\x01" if on else b"\x00")
        b.stub(0x0809AC60, ac60)
        b.stub(0x0804C8B4, lambda board: self.calls.append(("recplay", board.arg(1), board.arg(3))) and None)
        b.stub(0x080A16C4, lambda board: None)
        b.stub(0x08051738, lambda board: None)   # engine command ring: nothing drains it here
        for fn in (0x0809F820, 0x0809843C, 0x0809D240):
            b.stub(fn, (lambda f: lambda board: self.calls.append((hex(f),)) and None)(fn))
        b.mu.mem_write(APP_OBJ + 0x18, struct.pack("<IH", 0x080EAF38, 0x123))
        b.mu.mem_write(MX_STATE, b"\0" * 0x60)


def _ref(b, key):
    b.mu.mem_write(MIX_EV + 0x60, struct.pack("<IH", 0x080EAF38, key))
    return MIX_EV + 0x60


def t_seqrec_params(bench, res):
    b = _mix_setup(bench, build=False)
    if b.call(0x0808E305, 0x3E) == 0:
        b.call(0x0808C159)
    title = lambda i: b.cstr(b.call(0x0808E221, i))
    res.check(title(SR_LEN) == "Rec Len:" and b.call(0x0808E2A5, SR_LEN) == 11, f"Rec Len registered ({title(SR_LEN)!r})")
    res.check(title(SR_DEF) == "Seq Default:" and b.call(0x0808E2A5, SR_DEF) == 10, f"Seq Default registered ({title(SR_DEF)!r})")
    res.check(title(SR_SET) == "Rec Len Set:", f"Rec Len Set registered ({title(SR_SET)!r})")
    res.check(title(0x2D) != "" and title(0x19B) != "", "the chained stock registration still ran")
    res.check(b.call(0x0808E305, 0x86) == 512, f"Step Count max {b.call(0x0808E305, 0x86)}")
    img = b.image
    u16 = lambda a: struct.unpack_from("<H", img, a - bd.BASE)[0]
    res.check(u16(0x080EEF30) == SR_LEN and u16(0x080EEF32) == 0, "Seq page 0 lists Rec Len")
    res.check(u16(0x080EEEE8) == SR_LEN and u16(0x080EEEEA) == 0, "Seq page 0 (MIDI) lists Rec Len")
    res.check(u16(0x080EED66) == SR_DEF and u16(0x080EED68) == 0x1A0 and u16(0x080EED6A) == 0x1A1
              and u16(0x080EED6C) == 0, "Tools -> Rec lists Seq Default, Loop BPM, Dbl Copy")
    res.check(title(0x1A0) == "Loop BPM:" and b.call(0x0808E2A5, 0x1A0) == 5, f"Loop BPM registered ({title(0x1A0)!r})")
    res.check(title(0x1A1) == "Dbl Copy:" and b.call(0x0808E2A5, 0x1A1) == 2, f"Dbl Copy registered ({title(0x1A1)!r})")


def t_seqrec_templates(bench, res):
    b = _mix_setup(bench, build=False)
    adds = []
    b.stub(0x08093EF6, lambda board: adds.append((board.arg(1), board.arg(2))))
    b.call(bench.sym["seqrec_global_tpl"], 0x30040000, 0x19B, 0)
    mixer = [(0x1D8, 0)] if "mx_global_tpl" in bench.sym else []    # 3.1.T: Tools -> Clock BPM
    res.check(adds == [(0x19B, 0), (SR_DEF, 4), (0x1A0, 2), (0x1A1, 1)] + mixer,
              f"global template adds Seq Default = 1 bar, Loop BPM 80-160, Dbl Copy On {adds}")
    for glob, want in ((4, 4), (0, 0), (6, 6), (99, 4)):
        adds.clear()
        b.stub(P_GET_FN, lambda board, g=glob: g)
        b.call(bench.sym["seqrec_seq_tpl"], 0x30040000, 0x7A, 0)
        res.check(adds == [(0x7A, 0), (SR_LEN, want), (SR_SET, 0)], f"sequence template follows the global ({glob}) {adds}")


def t_seqrec_layer(bench, res):
    b = _mix_setup(bench, build=False)
    from unicorn import arm_const as A
    hit = []
    for a in (0x0809FE9C, 0x0809FE3C, 0x080A0296, 0x08099676, 0x08099648, 0x08099684):
        b.stub(a, (lambda x: lambda board: hit.append(x))(a))
    for fn, reg, cases in (("seqrec_set_layer", A.UC_ARM_REG_R5, ((0xBE, 0x0809FE9C), (SR_LEN, 0x080A0296), (SR_SET, 0x080A0296), (0xD0, 0x0809FE3C))),
                           ("seqrec_get_layer", A.UC_ARM_REG_R8, ((0xBE, 0x08099676), (SR_LEN, 0x08099684), (SR_SET, 0x08099684), (0xD0, 0x08099648)))):
        for ident, want in cases:
            hit.clear()
            b.mu.reg_write(reg, ident)
            b.call(bench.sym[fn], 0, 0, 0, (ident - 0xB7) & 0xFFFF)
            res.check(hit == [want], f"{fn} id {ident:#x} -> {[hex(h) for h in hit]}")


def t_seqrec_setparam(bench, res):
    for val, step_idx, events, want in ((5, 10, 0, [(SR_LEN, 5), (SR_SET, 1), (0x86, 32)]),
                                       (1, 10, 0, [(SR_LEN, 1), (SR_SET, 1), (0x86, 2)]),
                                       (9, 10, 0, [(SR_LEN, 9), (SR_SET, 1), (0x86, 512)]),
                                       (1, 6, 0, [(SR_LEN, 1), (SR_SET, 1), (0x85, 10), (0x86, 2)]),
                                       (0, 10, 0, [(SR_LEN, 0), (SR_SET, 1)])):
        w = _SeqWorld(bench, step_idx=step_idx, events=events)
        sp_arg = 0x2001F700
        w.b.call(bench.sym["seqrec_setparam"], APP_OBJ, _ref(w.b, 0x123), SR_LEN, val, 0)
        res.check(w.sets == want, f"Rec Len {val} with step index {step_idx}: {w.sets}")
    w = _SeqWorld(bench)
    w.b.call(bench.sym["seqrec_setparam"], APP_OBJ, _ref(w.b, 0x023), SR_LEN, 5, 0)
    res.check(w.sets == [(SR_LEN, 5)], f"a non-sequence key only passes through {w.sets}")


def t_seqrec_sync(bench, res):
    """Editing Step Count / Step Len on the settings page: Rec Len follows the length."""
    ev = [(1, 0, 240, 1, 60, 0)]
    for n, si, rl, events, ident, val, want, txt in (
            (16, 10, 4, ev, 0x86, 15, [(0x86, 15), (SR_LEN, 10)], "15/16"),
            (15, 10, 10, ev, 0x86, 32, [(0x86, 32), (SR_LEN, 5)], "32/16"),
            (15, 10, 10, ev, 0x85, 12, [(0x85, 12)], "15/32"),
            (16, 10, 0, (), 0x86, 15, [(0x86, 15), (SR_LEN, 10)], "15/16"),
            (15, 10, 0, (), 0x86, 16, [(0x86, 16)], "16/16"),
            (16, 10, 4, ev, 0x86, 300, [(0x86, 300), (SR_LEN, 10)], "300/16"),
            (16, 10, 4, ev, 0x85, 11, [(0x85, 11), (SR_LEN, 10)], "16st")):
        w = _knob_world(bench, n, step_idx=si, rec_len=rl, events=events)
        w.b.call(bench.sym["seqrec_setparam"], APP_OBJ, _ref(w.b, 0x123), ident, val, 0)
        res.check(w.sets == want and w.b.cstr(SR_TXT) == txt and ("post", 0x66) in w.ops,
                  f"{ident:#x} = {val} on {n} steps, Rec Len {rl}: {w.sets} {w.b.cstr(SR_TXT)!r} {w.ops}")
    w = _knob_world(bench, 15, rec_len=10, events=ev)
    w.b.call(bench.sym["seqrec_setparam"], APP_OBJ, _ref(w.b, 0x123), SR_LEN, 10, 0)
    res.check(w.sets == [(SR_LEN, 10), (SR_SET, 1)], f"picking the own length keeps the length {w.sets}")
    w = _knob_world(bench, 63, rec_len=10, events=ev)
    w.b.stub(0x0808E278, lambda board: 0x1234)
    r = w.b.call(bench.sym["sr_labels_hook"], SR_LEN)
    res.check(r == 0x1234 and w.b.cstr(SR_TXT) == "63/16", f"settings row build fills the label first ({w.b.cstr(SR_TXT)!r})")
    w.b.unstub(0x0808E278)
    w.b.call(0x0808C159)
    labels = w.b.call(0x0808E279, SR_LEN)
    res.check(w.b.u32(labels + 40) == SR_TXT, "Rec Len value 10 is labelled from RAM")


def t_seqrec_clear(bench, res):
    for touched, glob, want in ((0, 0, [(SR_LEN, 0), (SR_SET, 0)]), (0, 4, [(SR_LEN, 4), (SR_SET, 0)]),
                                (1, 6, [(SR_LEN, 6), (SR_SET, 0)]), (1, 0, [(SR_LEN, 0), (SR_SET, 0)])):
        w = _SeqWorld(bench, rec_len=5, touched=touched, glob=glob)
        w.b.call(bench.sym["seqrec_clear"], APP_OBJ, _ref(w.b, 0x123))
        res.check(w.calls[:1] == [("0x809f820",)] and w.sets == want, f"CLR touched={touched} default={glob}: {w.sets}")
    w = _SeqWorld(bench, rec_len=5, touched=1, glob=0)
    w.b.call(bench.sym["seqrec_clear_all"], APP_OBJ, _ref(w.b, 0x123))
    res.check(w.sets == [(SR_LEN, 0), (SR_SET, 0)], f"clear all resets to Seq Default {w.sets}")


def t_seqrec_record(bench, res):
    w = _SeqWorld(bench, rec_len=0)
    b = w.b
    res.check(b.call(bench.sym["sr_eligible"], APP_OBJ) == 1, "empty stopped Custom sequence is eligible")
    b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(b.u32(MX_STATE + 80) == 1 and not any(c[0] == "recplay" for c in w.calls),
              f"REC with the transport stopped arms the looper {w.calls}")
    msg = MIX_EV + 0x80
    b.mu.mem_write(msg, b"\0" * 0x20)
    b.w32(msg + 0xC, 1)
    w.ticks = 1000
    w.sets.clear()
    b.call(bench.sym["seqrec_recstate"], APP_OBJ, msg)
    res.check(w.sets == [(0x86, 512)] and b.u32(MX_STATE + 48) == 1 and b.u32(MX_STATE + 56) == 3840,
              f"recording on: grow to 512 steps, start at the next bar {w.sets} start={b.u32(MX_STATE + 56)}")
    b.w32(msg + 8, 0x010203)          # engine key: kind 1, row 2, col 3 -> UI 0x123
    w.ticks = 3900
    b.call(bench.sym["seqrec_started"], APP_OBJ, msg, 1)
    res.check(b.u32(MX_STATE + 56) == 3840 and b.u32(MX_STATE + 64) == 1, "start taken from the sequence-started message")
    b.w32(msg + 0xC, 0)
    w.ticks = 3840 + 9600             # 2.5 bars in
    w.sets.clear()
    w.calls.clear()
    b.call(bench.sym["seqrec_recstate"], APP_OBJ, msg)
    res.check(w.sets == [(0x86, 48)], f"REC again: loop closes at the next bar (3 bars = 48 steps) {w.sets}")
    res.check(w.calls and w.calls[0][0] == "ac60" and w.calls[0][2] == [(0x86, 48)], "length set before stock finalises notes")
    res.check(b.u32(MX_STATE + 48) == 0, "custom recording done")
    for kw, why in ((dict(events=3), "not empty"), (dict(playing=1), "transport playing")):
        w = _SeqWorld(bench, **kw)
        w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
        res.check(not any(c[0] == "recplay" for c in w.calls) and w.b.u32(MX_STATE + 80) == 0,
                  f"REC neither arms nor starts the transport when {why}")
    w = _SeqWorld(bench, rec_len=4)
    w.b.mu.mem_write(msg, b"\0" * 0x20)
    w.b.w32(msg + 0xC, 1)
    w.b.call(bench.sym["seqrec_recstate"], APP_OBJ, msg)
    res.check(not w.sets and w.b.u32(MX_STATE + 48) == 0, "a 1-bar sequence records as stock")


def t_looper(bench, res):
    """Looper take: REC arms, the first note starts, REC again sets tempo and starts playback."""
    b = bench.b
    qtab = struct.unpack("<16f", b.image[0x080ECC70 - bd.BASE: 0x080ECC70 - bd.BASE + 64])
    q16 = qtab.index(0.25)
    w = _knob_world(bench, 16, rec_len=0, dbl=2)               # Loop BPM 80-160
    w.l0[0x46] = q16
    w.l0[0xB7] = 1
    b = w.b
    leds, ops, ring = [], [], []
    b.stub(0x080436F8, lambda board: leds.append(board.arg(2)) and None)
    def post(board):
        m = board.arg(1)
        if board.u8(m) == 0x39:
            ops.append(("param", board.u32(m + 8), board.u32(m + 12), board.u32(m + 16)))
        else:
            ops.append(("msg", board.u8(m), board.u32(m + 8)))
    b.stub(0x08051738, post)
    b.stub(0x08097C1C, lambda board: ops.append(("bpm", board.u32(board.arg(1) + 0xC))) and None)
    b.stub(0x08098288, lambda board: ops.append(("launch", board.u16(board.arg(1) + 4))) and None)

    def pop(board):
        if not ring:
            return 0
        typ, note, origin = ring.pop(0)
        e = bytearray(0x30)
        e[0] = typ
        struct.pack_into("<H", e, 2, origin)
        struct.pack_into("<I", e, 0x10, note)
        e[0x18] = 1
        board.mu.mem_write(board.arg(1), bytes(e))
        return 1
    b.stub(0x0804C754, pop)
    buf = MIX_EV + 0x100

    def feed(stamp, *entries):
        b.w32(SE_OBJ, stamp)
        ring.extend(entries)
        while True:
            r = b.call(bench.sym["looper_pop"], 0, buf)
            if not r:
                break

    b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(b.u32(MX_STATE + 80) == 1 and leds[-1:] == [1], f"REC arms, LED on {leds}")
    allkeys = {0x10000 | r << 8 | c for r in range(4) for c in range(4)}
    targets = lambda v: {o[1] for o in ops if o[0] == "param" and o[2] == 0x6E and o[3] == v}
    res.check(ops[0] == ("param", 0x400, 0xAF, 0x010203), f"arming re-sends the engine's selected sequence {ops[:1]}")
    res.check(targets(1) == allkeys and ("param", 0x010203, 0xB7, 1) in ops,
              f"all 16 sequence players become record targets, DispMode re-sent {len(ops)}")
    ops.clear()
    for tick, want in ((0, 0), (256, 1), (512, 0)):
        b.w32(UWTICK, tick)
        feed(0)
        res.check(leds[-1] == want, f"armed LED blinks: uwTick {tick} -> {leds[-1]}")
    feed(500, (6, 60, 0x101))
    res.check(b.u32(MX_STATE + 80) == 1 and b.u32(MX_STATE + 88) == 0, "a note-off doesn't start the take")
    t0 = 1000
    feed(t0, (5, 60 | 100 << 16, 0x101))
    res.check(b.u32(MX_STATE + 80) == 2 and b.u32(MX_STATE + 84) == t0 and leds[-1] == 1, "first note-on starts the take, LED on")
    feed(t0 + 24000, (6, 60, 0x101))                  # 0.5 s
    feed(t0 + 96960, (5, 64 | 90 << 16, 0x101))       # 2.02 s, held to the end
    feed(t0 + 100000, (7, 1, 0))                      # sequence playback: ignored
    res.check(b.u32(MX_STATE + 88) == 3, f"three notes buffered ({b.u32(MX_STATE + 88)})")
    b.w32(SE_OBJ, t0 + 384000)                        # 8 s take
    w.sets.clear()
    b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    got = [(e[1], e[2], e[4], e[0]) for e in w.events]
    res.check(got == [(0, 960, 60 | 100 << 16, 0x1010001), (3840, 15360 - 3878, 64 | 90 << 16, 0x1010001)],
              f"notes written quantised to 1/16 at 4 bars (120 BPM in 80-160) {got}")
    res.check((0x86, 64) in w.sets and w.ops[0] == "snap" and ("push", 1) in w.ops, f"64 steps, undo, push {w.sets} {w.ops}")
    res.check(targets(0) == allkeys, "finishing clears the record targets")
    ops[:] = [o for o in ops if o[0] != "param"]
    res.check(ops == [("msg", 0x20, 120000), ("bpm", 120000), ("launch", 0x123), ("msg", 0x73, 0), ("msg", 0x74, 0)],
              f"tempo 120.000, sequence launched, transport started without lead-in {ops}")
    res.check(b.u32(MX_STATE + 80) == 0 and leds[-1] == 0, "looper idle, LED off")

    w = _knob_world(bench, 16, rec_len=0, dbl=2)
    w.b.stub(0x08051738, post)
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(w.b.u32(MX_STATE + 80) == 0, "REC again before any note disarms")
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    w.b.mu.mem_write(SE_OBJ + 0x38, b"\x01")
    ring.clear()
    w.b.call(bench.sym["looper_pop"], 0, buf)
    res.check(w.b.u32(MX_STATE + 80) == 0, "the transport starting disarms")
    w = _knob_world(bench, 16, rec_len=0, dbl=0)               # 60-120
    w.b.stub(0x08051738, post)
    w.l0[0xB7] = 1
    ops.clear()
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    w.b.w32(SE_OBJ, 5000)
    ring.append((5, 60 | 100 << 16, 0x101))
    w.b.call(bench.sym["looper_pop"], 0, buf)
    w.b.w32(SE_OBJ, 5000 + 72000)                     # 1.5 s: 1 bar = 160 BPM, too fast for 60-120
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    ops2 = [o for o in ops if o[0] != "param"]
    res.check(ops2[:2] == [("msg", 0x20, 160000), ("bpm", 160000)] and (0x86, 16) in w.sets,
              f"short take, range 60-120: 1 bar at 160 BPM is the closest {ops2[:2]} {w.sets}")
    w = _knob_world(bench, 16, rec_len=0, dbl=2)
    w.b.stub(0x08051738, post)
    ops.clear()
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    w.b.w32(SE_OBJ, 5000)
    ring.append((5, 0, 0x101))
    w.b.call(bench.sym["looper_pop"], 0, buf)
    w.b.w32(SE_OBJ, 5000 + 6000)                      # 0.125 s
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(all(o[0] == "param" for o in ops) and not w.events and w.b.u32(MX_STATE + 80) == 0
              and targets(0) == allkeys, f"a take under 0.25 s is dropped, targets cleared")
    w = _knob_world(bench, 16, rec_len=4, dbl=2)               # empty, Rec Len 1 bar
    w.b.stub(0x08051738, post)
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(w.b.u32(MX_STATE + 80) == 1, "any empty stopped sequence arms, whatever its Rec Len")
    w = _knob_world(bench, 16, rec_len=0, dbl=2)
    w.b.stub(0x08051738, post)
    w.l0[0xBE] = 1                                             # queued to start with PLAY
    w.b.call(bench.sym["seqrec_rec_press"], APP_OBJ, 0)
    res.check(w.b.u32(MX_STATE + 80) == 1, "an empty sequence queued for PLAY arms too")


def t_env_source(bench, res):
    """Clip/Slicer: ENV mod source (amp ADSR per voice) and Env Trig (slice-triggered envelope)."""
    b = _mix_setup(bench, build=False)
    img = b.image
    if b.call(0x0808E305, 0x3E) == 0:
        b.call(0x0808C159)
    title = lambda i: b.cstr(b.call(0x0808E221, i))
    u16 = lambda a: struct.unpack_from("<H", img, a - bd.BASE)[0]
    u32 = lambda a: struct.unpack_from("<I", img, a - bd.BASE)[0]
    res.check(title(0x1A2) == "Env Trig:" and b.call(0x0808E2A5, 0x1A2) == 2, f"Env Trig registered ({title(0x1A2)!r})")
    res.check(u16(0x080EF90E) == 0xF5 and u16(0x080EF910) == 0x1A2 and u16(0x080EF912) == 0,
              "Slicer Conf: Env Trig after Pad Note")
    res.check(u16(0x080EF206) == 0xF5 and u16(0x080EF208) == 0x1A2 and u16(0x080EF20A) == 0,
              "Slicer Conf (type-0x4C list): Env Trig after Pad Note")
    res.check(all(u16(a - 2) == 0xF5 and u16(a) == 0x1A2 and u16(a + 2) == 0 for a in (0x080EFA78, 0x080EF370)),
              "Clip Conf (both lists): Env Trig after Pad Note")
    names, codes = bench.sym["env_src_names"], bench.sym["env_src_codes"]
    res.check(all(u32(a) == names for a in (0x0808CCC0, 0x080B6964))
              and all(u32(a) == codes for a in (0x0809A69C, 0x080B696C, 0x080B7B00)), "source list readers repointed")
    res.check(img[0x0808CB3E - bd.BASE: 0x0808CB42 - bd.BASE].hex() == "09210091" and img[0x080B67CC - bd.BASE] == 9
              and img[0x080B7994 - bd.BASE] == 9, "source count 9")
    labels = [b.cstr(b.u32(names + 4 * i)) for i in range(9)]
    res.check(labels[2] == "LFO" and labels[8] == "ENV" and b.u16(codes + 16) == 0x30, f"9th source ENV {labels}")
    clip, msg = 0x30030000, 0x30033000
    b.mu.mem_write(clip, b"\0" * 0xC00)
    def route(target, slot, src):
        b.mu.mem_write(msg, b"\0" * 0x18)
        b.mu.mem_write(msg + 0xC, struct.pack("<HxxHxxHh", target, slot, src, 500))
        b.call(bench.sym["env_route"], clip, msg)
    route(0xC8, 1, 0x30)
    res.check(b.u32(clip + 0x448 + 16) == clip + 0x4DC and b.u32(clip + 0x858 + 16) == clip + 0x8EC
              and b.u16(msg + 0x14) == 0x30, f"ENV -> Cutoff: each voice's slot reads its envelope level "
              f"{b.u32(clip + 0x448 + 16):#x} {b.u32(clip + 0x858 + 16):#x}")
    route(0x62, 0, 0x30)
    res.check(b.u32(clip + 0x26C + 8) == clip + 0x4DC and b.u32(clip + 0x67C + 8) == clip + 0x8EC, "ENV -> Pan")
    route(0x5C, 2, 0x30)
    res.check(b.u32(clip + 0x2F4 + 24) == clip + 0x4DC and b.u32(clip + 0x704 + 24) == clip + 0x8EC
              and b.u32(clip + 0x2F4 + 28) != 0, f"ENV -> Pitch: both voices, amount set "
              f"{b.u32(clip + 0x2F4 + 24):#x} {b.u32(clip + 0x704 + 24):#x}")
    # the real evaluator reads source pointer + 4: the envelope level must come out
    env0 = clip + 0x260 + 0x258
    b.mu.mem_write(env0 + 0x28, struct.pack("<f", 0.5))
    b.mu.mem_write(env0 + 0x2C, struct.pack("<f", 0.0))
    from unicorn import arm_const as A
    def evaluate(mp):
        b.call(0x0806FB31, mp)
        return struct.unpack("<f", b.mu.mem_read(mp + 0x30, 4))[0]
    mp = clip + 0x2F4                                # Pitch, voice 0 (routed above, slot 2)
    b.mu.mem_write(mp + 0x28, struct.pack("<ff", -1000.0, 1000.0))
    v_half = evaluate(mp)
    b.mu.mem_write(env0 + 0x28, struct.pack("<f", 1.0))
    v_full = evaluate(mp)
    res.check(v_half != 0 and abs(v_full - 2 * v_half) < 1e-4, f"evaluator follows the envelope level: 0.5 -> {v_half}, 1.0 -> {v_full}")
    route(0xC8, 2, 4)
    res.check(b.u32(clip + 0x448 + 24) == clip + 0x388, f"LFO routing unchanged {b.u32(clip + 0x448 + 24):#x}")
    route(0xC8, 0, 0xD)
    res.check(b.u32(clip + 0x448 + 8) == clip + 0x2A8 and b.u32(clip + 0x858 + 8) == clip + 0x6B8, "VEL routing unchanged")
    # Env Trig: param path -> table, slice change retriggers a held envelope
    pid = 0x0102                                          # row 1, col 2
    b.mu.mem_write(clip + 0x18, struct.pack("<I", pid))
    pmsg = 0x30033100
    b.mu.mem_write(pmsg, b"\0" * 0x18)
    b.mu.mem_write(pmsg + 0xC, struct.pack("<II", 0x1A2, 1))
    b.call(bench.sym["hook_clip_param"], clip, 0, pmsg)
    packed = 1 << 2 | 2
    res.check(b.call(bench.sym["envtrig_read"], 0, packed) == 1, "engine param 0x1A2 = Slice reaches the table")
    b.call(bench.sym["envtrig_post"], 0, pid, 0x1A2, 0)
    res.check(b.call(bench.sym["envtrig_read"], 0, packed) == 0, "UI post 0x1A2 = Note reaches the table")
    b.call(bench.sym["envtrig_post"], 0, pid, 0x1A2, 1)
    voice = clip + 0x260
    env = voice + 0x258
    gates, ret = [], [3]
    def gate(board):
        gates.append(board.arg(0))
        board.mu.mem_write(board.arg(0) + 0x1C, struct.pack("<f", 0.0))
    b.stub(0x0806A5F8, gate)
    b.stub(0x08065550, lambda board: ret[0])
    for stage, r, trig, want, why in ((2, 3, 1, 1, "held note, Slice: retrigger"),
                                      (3, 3, 1, 1, "sustain: retrigger"),
                                      (4, 3, 1, 0, "released: no retrigger"),
                                      (2, 0xFFFFFFFF, 1, 0, "no grain: no retrigger"),
                                      (2, 3, 0, 0, "Env Trig Note: stock")):
        gates.clear()
        ret[0] = r
        b.call(bench.sym["envtrig_write"], trig, packed)
        b.mu.mem_write(env + 4, bytes([stage]))
        b.mu.mem_write(env + 0x1C, struct.pack("<f", 0.7))
        got = b.call(bench.sym["env_slice"], clip, voice, 0, 0)
        level = struct.unpack("<f", b.mu.mem_read(env + 0x1C, 4))[0]
        res.check(len(gates) == want and got == r and (not want or (gates == [env] and abs(level - 0.7) < 1e-6)),
                  f"{why} (gates {[hex(g) for g in gates]}, ret {got:#x}, level {level})")
    b.unstub(0x0806A5F8)
    b.unstub(0x08065550)


def t_pset_first_row(bench, res):
    """Preset list: the loaded preset when it is listed, otherwise the first row."""
    b = bench.b
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    stock = open(STOCK_IMAGE, "rb").read()
    for va in (0x080BDE96, 0x080BDFEE):
        res.check(b.image[va - bd.BASE: va - bd.BASE + 4] == stock[va - bd.BASE: va - bd.BASE + 4],
                  f"{va:#x}: loaded-preset name lookup is stock")
    for va in (0x080BDEA4, 0x080BDFF8):
        insn = next(md.disasm(bytes(b.image[va - bd.BASE: va - bd.BASE + 4]), va))
        res.check(insn.mnemonic == "bl" and int(insn.op_str.lstrip("#"), 16) == bench.sym["pset_select"] & ~1,
                  f"{va:#x}: select by name goes through pset_select")
    calls = []
    found = [1]
    b.stub(0x080BD8C0, lambda board: calls.append("byname") or found[0])
    b.stub(0x080B074C, lambda board: calls.append(("index", board.arg(0), board.arg(1))) and None)
    b.stub(0x080BD7D4, lambda board: calls.append(("draw", board.arg(1), board.arg(2))) and None)
    scr = 0xC2000000
    r = b.call(bench.sym["pset_select"], scr, 0x2001C600)
    res.check(r == 1 and calls == ["byname"], f"loaded preset listed: selected, nothing else {calls}")
    calls.clear()
    found[0] = 0
    r = b.call(bench.sym["pset_select"], scr, 0x2001C600)
    res.check(calls == ["byname", ("index", scr + 0x618, 0), ("draw", 0, 1)], f"not listed: first row {calls}")
    for fn in (0x080BD8C0, 0x080B074C, 0x080BD7D4):
        b.unstub(fn)


def t_mix_button(bench, res):
    b = _mix_setup(bench, build=False)
    calls = []
    b.stub(SET_SCREEN, lambda board: calls.append(board.arg(1)))
    for scr in (0x2E, 0x2F):
        b.mu.mem_write(APP_OBJ + 0x8CA4, bytes([scr]))
        for want in (0x2E, 0x2F):
            b.call(bench.sym["mix_btn_screen"], APP_OBJ, want, 0, 0)
    res.check(calls == [], f"MIX on Mix stays on Mix, whatever stock picks {calls}")
    b.mu.mem_write(APP_OBJ + 0x8CA4, b"\x14")
    b.call(bench.sym["mix_btn_screen"], APP_OBJ, 0x2F, 0, 0)
    res.check(calls == [0x2E], f"MIX elsewhere opens Mix {calls}")


def t_mix_momentary(bench, res):
    """MIX on Mix is momentary: second layer, select and the plain mixer view only while held."""
    b = _mix_setup(bench)
    ids = lambda: {n: _fader(b, n)["id"] for n in MIX_FADERS}
    _mix_tick(bench)
    for down, t, want, view, why in ((0, 1000, MIX_HELD, 0, "held: MIX layer, plain view"),
                                     (1, 1150, MIX_MAIN, 1, "short press released: main layer"),
                                     (0, 3000, MIX_HELD, 0, "held again"),
                                     (1, 4000, MIX_MAIN, 1, "long hold released: main layer")):
        b.w32(UWTICK, t)
        b.mu.mem_write(BTN_MIX, bytes([down]))
        _mix_tick(bench)
        res.check(ids() == want and b.u8(MIX_OBJ + 0x1E40) == view and b.u32(MX_STATE + 112) == (1 - down),
                  f"{why} {ids()} mode={b.u8(MIX_OBJ + 0x1E40)}")
    img = bench.b.image
    res.check(img[0x080AB594 - bd.BASE: 0x080AB598 - bd.BASE] != bytes.fromhex("00f0faf8"), "header tick call site hooked")


def t_header_sel(bench, res):
    """Header bar: "Sel" between the counter and Seq x while PADS / SEQS / MIX (on Mix) is held."""
    b = _mix_setup(bench, build=False)
    hdr = 0x30018000
    b.mu.mem_write(hdr, b"\0" * 0x200)
    b.w32(hdr + 0x140, 140)
    texts, ticks = [], []
    b.stub(0x080C3B98, lambda board: texts.append((board.arg(0) - hdr, board.cstr(board.arg(1)))) and None)
    b.stub(0x080AB78C, lambda board: ticks.append(1) and None)
    tick = lambda: b.call(bench.sym["hdr_tick"], hdr, 0)
    b.mu.mem_write(APP_OBJ + 0x8CA4, b"\x14")
    for btn in (0x24009ABE, 0x24009AEE):
        b.mu.mem_write(btn, b"\x01")
    tick()
    res.check(b.u32(hdr + 0x140) == 170 and b.u32(hdr + 0x148) == 84 and b.u32(hdr + 0xE8) == 90 and b.u32(hdr + 0xF0) == 36 and b.u32(hdr + 0x40) == 70
              and ticks == [1] and not texts, f"Seq x moved right, Sel slot placed, stock tick runs {texts}")
    for btn, name in ((0x24009ABE, "PADS"), (0x24009AEE, "SEQS")):
        texts.clear()
        b.mu.mem_write(btn, b"\x00")
        tick()
        tick()
        b.mu.mem_write(btn, b"\x01")
        tick()
        res.check(texts == [(0xE4, "Sel"), (0xE4, "")], f"{name} held: Sel, released: cleared {texts}")
    texts.clear()
    b.w32(MX_STATE + 112, 1)
    tick()
    res.check(not texts, "MIX held off Mix: no Sel")
    b.mu.mem_write(APP_OBJ + 0x8CA4, b"\x2E")
    tick()
    b.w32(MX_STATE + 112, 0)
    tick()
    res.check(texts == [(0xE4, "Sel"), (0xE4, "")], f"MIX held on Mix: Sel {texts}")


# ---- Tools -> Master (master.S)

M_GLOBAL_BAG = 0x24028D18
REGISTER_ALL = 0x0808C159
P_TITLE, P_FMT, P_MIN, P_MAX = 0x0808E221, 0x0808E24D, 0x0808E2D1, 0x0808E305
BAG_ADD = 0x08093EF6
PAGE_MODEL = 0x080996B0
COMP_PROC = 0x0804EDDC
COMP_CTOR = 0x0804F010
MASTER_IDS = [0xB4, 0x131, 0x132, 0x12F, 0x130, 0x13A, 0x1A8, 0x1A9]
DEFAULTS = [(0x131, -4000), (0x132, 4), (0x12F, 10), (0x130, 250), (0x13A, -4000),
            (0x1A8, 4), (0x1A9, 750)]
# offset -> (kind, stock ctor value)
FIELDS = {0x428: ("f", -4.0), 0x42C: ("f", 4.0), 0x430: ("f", -4.0),
          0x448: ("i", 480), 0x44C: ("i", 12000), 0x468: ("i", 192), 0x46C: ("i", 36000)}
BAG_ENTRIES = 0x30040000
COMP = 0x30010000
MIXER = 0x30018000
MODEL = 0x2001C900
REF = 0x2001C880


def _registry(b):
    if b.call(P_MAX, 0x3E) == 0:
        b.call(REGISTER_ALL)


def _set_bag(b, entries):
    blob = b"".join(struct.pack("<HHi", i, 0, v) for i, v in entries)
    b.mu.mem_write(BAG_ENTRIES, blob)
    b.w32(M_GLOBAL_BAG + 4, BAG_ENTRIES)
    b.mu.mem_write(M_GLOBAL_BAG + 0xA, struct.pack("<H", len(entries)))


def _fields(b):
    out = {}
    for off, (kind, _) in FIELDS.items():
        raw = bytes(b.mu.mem_read(COMP + off, 4))
        out[off] = struct.unpack("<f" if kind == "f" else "<i", raw)[0]
    return out


def _run_comp(bench, b):
    seen = []
    b.stub(COMP_PROC, lambda board: seen.append((board.arg(0), board.arg(1))) or 1)
    b.mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))      # FPU on
    b.call(bench.sym["master_comp"], COMP, MIXER)
    return seen


def t_master_image(bench, res):
    img = bench.b.image
    main = _u16s(img, 0x080EED18, 10)
    res.check(main == [0xBA, 0xBB, 0xBC, 0xBD, 0x7F, 0xCD, 0xF6, 0xF7, 0x19B, 0],
              f"Tools -> Main without Compressor {[hex(x) for x in main]}")
    page4 = _u16s(img, 0x080EEE38, 9)
    res.check(page4 == MASTER_IDS + [0], f"Tools -> Master lists the eight ids {[hex(x) for x in page4]}")
    res.check(_u16s(img, 0x080EED60, 1) == [0x9D], "Tools -> Rec list untouched")
    name = struct.unpack_from("<I", img, 0x080EEE90 - bd.BASE)[0]
    if "dj_tools_tick" in bench.sym:            # 3.1.T: Master is in the FX row instead
        res.check(name == 0x080CB060, "tab name 4 stays empty (Master is in the FX row)")
    else:
        res.check(name == bench.sym["s_master"], "tab name 4 points at \"Master\"")
    res.check(struct.unpack_from("<I", img, 0x080EEE94 - bd.BASE)[0] == 0x080CB060,
              "tab name 5 stays empty (5 tabs)")
    res.check(img[0x080996D2 - bd.BASE:0x080996D4 - bd.BASE].hex() == "042e"
              and img[0x08098D5A - bd.BASE:0x08098D5C - bd.BASE].hex() == "042e",
              "Tools page index up to 4 (page model and row builder)")


def t_master_registry(bench, res):
    b = bench.b
    _registry(b)
    title = lambda i: b.cstr(b.call(P_TITLE, i))
    want = {0x131: "C Thresh:", 0x132: "C Ratio:", 0x12F: "C Attack:", 0x130: "C Release:",
            0x13A: "C Gain:", 0x1A8: "L Attack:", 0x1A9: "L Release:"}
    for i, t in want.items():
        res.check(title(i) == t, f"{i:#x} registered as {title(i)!r}")
    res.check(b.call(P_FMT, 0x1A8) == 0xF and b.call(P_FMT, 0x1A9) == 0xE,
              "L Attack / L Release use the C Attack / C Release formats (ms)")
    res.check(b.call(P_MIN, 0x1A8) == 0 and b.call(P_MAX, 0x1A8) == 10000
              and b.call(P_MAX, 0x1A9) == 10000, "L Attack / L Release range 0..10000 ms")
    res.check(title(0x13B) != "" and title(0x189) != "", "the chained stock 0x13B and later registrations ran")
    res.check(title(0x19C) != "" and title(0x1A1) == "Dbl Copy:", "3.1.Q registrations still ran")


def t_master_template(bench, res):
    b = bench.b
    adds = []
    b.stub(BAG_ADD, lambda board: adds.append((board.arg(1), board.arg(2) - (1 << 32) if board.arg(2) >> 31 else board.arg(2))))
    b.call(bench.sym["master_tpl"], 0x30060000, 0xB4, 1)
    res.check(adds == [(0xB4, 1)] + DEFAULTS, f"global template adds Compressor then the defaults {adds}")


def t_master_page(bench, res):
    b = bench.b
    _set_bag(b, [(0xB4, 1)] + DEFAULTS)
    b.mu.mem_write(REF, b"\0" * 8)
    b.mu.mem_write(REF + 4, struct.pack("<H", 0x40))

    def model(page):
        b.mu.mem_write(MODEL, b"\0" * 0x80)
        ok = b.call(PAGE_MODEL, APP_OBJ, REF, MODEL, page)
        m = bytes(b.mu.mem_read(MODEL, 0x80))
        ids = [x for x in struct.unpack_from("<8H", m, 2) if x]
        names = [m[0x4D + 8 * i:0x4D + 8 * i + 8].split(b"\0")[0].decode() for i in range(6)]
        count = struct.unpack_from("<I", m, 0x44)[0]
        return ok, ids, names, count, m
    ok, ids, names, count, m = model(4)
    res.check(ok == 1 and ids == MASTER_IDS, f"page 4 model holds the Master ids {[hex(i) for i in ids]}")
    if "dj_tools_tick" in bench.sym:            # 3.1.T: Master is in the FX row, Tools keeps 4 tabs
        res.check(names[:5] == ["Main", "Rec", names[2], names[3], ""] and count == 4,
                  f"four tabs, page 4 still built {names} count {count}")
    else:
        res.check(names[:5] == ["Main", "Rec", names[2], names[3], "Master"] and count == 5,
                  f"five tabs {names} count {count}")
    vals = list(struct.unpack_from("<8i", m, 0x14))
    res.check(vals == [1] + [v for _, v in DEFAULTS], f"page 4 values from the global bag {vals}")
    ok, ids, *_ = model(0)
    res.check(ok == 1 and 0xB4 not in ids and ids[:4] == [0xBA, 0xBB, 0xBC, 0xBD], f"page 0 (Main) {[hex(i) for i in ids]}")
    ok, *_ = model(5)
    res.check(ok == 0, "no page 5")


def t_master_comp(bench, res):
    b = bench.b
    b.mu.mem_write(COMP, b"\0" * 0x700)
    _set_bag(b, [(0x131, -12500), (0x132, 8), (0x12F, 25), (0x130, 400), (0x13A, 3000),
                 (0x1A8, 2), (0x1A9, 1000)])
    seen = _run_comp(bench, b)
    f = _fields(b)
    res.check(seen == [(COMP, MIXER)], f"stock compressor runs with the same args {seen}")
    want = {0x428: -12.5, 0x42C: 8.0, 0x430: 3.0, 0x448: 25 * 48, 0x44C: 400 * 48, 0x468: 96, 0x46C: 48000}
    res.check(all(abs(f[k] - v) < 1e-5 for k, v in want.items()), f"fields set {f}")
    # out of range and zero times are clamped; missing ids leave their field alone
    b.mu.mem_write(COMP, b"\0" * 0x700)
    b.mu.mem_write(COMP + 0x46C, struct.pack("<i", 777))
    _set_bag(b, [(0x131, -90000), (0x132, 0), (0x12F, 0), (0x130, 99999), (0x13A, 50000), (0x1A8, -5)])
    _run_comp(bench, b)
    f = _fields(b)
    want = {0x428: -48.0, 0x42C: 1.0, 0x430: 36.0, 0x448: 1, 0x44C: 480000, 0x468: 1, 0x46C: 777}
    res.check(all(abs(f[k] - v) < 1e-5 for k, v in want.items()), f"clamped, missing kept {f}")
    _set_bag(b, [])
    seen = _run_comp(bench, b)
    res.check(seen == [(COMP, MIXER)], "an empty bag still runs stock")


def t_master_defaults_are_stock(bench, res):
    b = bench.b
    b.mu.mem_write(COMP, b"\0" * 0x700)
    b.call(COMP_CTOR, COMP)
    stock = _fields(b)
    res.check(all(stock[k] == v for k, (_, v) in FIELDS.items()), f"stock ctor values {stock}")
    _set_bag(b, [(0xB4, 1)] + DEFAULTS)
    _run_comp(bench, b)
    res.check(_fields(b) == stock, f"Master defaults reproduce the stock settings exactly {_fields(b)}")


def t_master_gui(bench, res):
    """Real GUI + App: Tools opens, tab 4 (Master) switches to screen 0x29 and lists the rows."""
    from unicorn import UC_HOOK_CODE
    b = bench.b
    mu = b.mu
    mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    b.unstub(SET_SCREEN)
    b.stub(0x080A2258, lambda board: 0)
    tick = [0]

    def gettick(board):
        tick[0] += 5
        board.w32(UWTICK, tick[0])
        return tick[0]
    b.stub(0x0807794C, gettick)
    for fn in GUI_DP_STUBS + GUI_DRAW_STUBS:
        b.stub(fn, lambda board: 0)
    texts = []
    mu.hook_add(UC_HOOK_CODE, lambda mu_, a, sz, d: texts.append(b.cstr(b.arg(1), 32)),
                begin=0x080C3B98, end=0x080C3B98)
    rows = []
    mu.hook_add(UC_HOOK_CODE, lambda mu_, a, sz, d: rows.append((b.arg(1) & 0xFFFF, b.arg(2))),
                begin=0x080A5038, end=0x080A5038)
    labels = []
    mu.hook_add(UC_HOOK_CODE, lambda mu_, a, sz, d: labels.append(b.cstr(b.arg(1), 16)),
                begin=0x080A3DEC, end=0x080A3DEC)
    gui = 0xC2000000
    mu.mem_write(gui, b"\0" * 0x100000)
    b.call(0x0808BE4C, gui)
    b.w32(0x24002E6C, gui)
    # the global bag as at boot: template, so the rows have values
    _set_bag(b, [(0xB4, 1)] + DEFAULTS)

    def pump():
        for _ in range(3):
            b.call(0x0808B654, gui)
            b.call(0x0808B2D0, gui, limit=50_000_000)

    def where():
        return b.u8(APP_OBJ + 0x8CA4), b.u32(gui + 0x280 + 0x34)
    mu.mem_write(APP_OBJ + 0x8CA4, b"\x02")
    mu.mem_write(APP_OBJ + 0x8CC3, b"\x00")
    b.call(SET_SCREEN, APP_OBJ, 0x25, 0, 0)
    pump()
    scr, page = where()
    res.check(scr == 0x25 and b.u32(page) == 0x080F18A4,
              f"Tools opens on Main (screen {scr:#x}, page +{page - gui:#x} vtable {b.u32(page):#x})")
    labels.clear()
    texts.clear()
    rows.clear()
    ev = 0x2001C600
    mu.mem_write(ev, b"\0" * 0x40)
    mu.mem_write(ev, struct.pack("<H", 1))
    b.w32(ev + 0xC, 0x12F + 4)
    b.call(0x080C5360, page, ev)                # Tools onChildEvent: tab 4 pressed
    pump()
    scr, page2 = where()
    m = bytes(mu.mem_read(page2 + 0xA560, 0x80))
    ids = [x for x in struct.unpack_from("<8H", m, 2) if x]
    res.check(b.u8(page2 + 0xA5E0) == 4, f"Tools page index 4 ({b.u8(page2 + 0xA5E0)}, screen {scr:#x})")
    res.check(ids == MASTER_IDS, f"Master page model {[hex(i) for i in ids]}")
    if "dj_tools_tick" in bench.sym:
        res.check("Master" not in labels and "Main" in labels, f"tab labels, no Master tab {labels}")
    else:
        res.check("Master" in labels and "Main" in labels, f"tab labels {labels}")
    vals = dict(DEFAULTS + [(0xB4, 1)])
    want = [(i, vals[i] & 0xFFFFFFFF) for i in MASTER_IDS]
    res.check(rows[-8:] == want, f"Master list rows (id, value) {[(hex(i), v) for i, v in rows[-8:]]}")
    # the stock path for a Tools tab: App_SetScreen 0x29
    b.call(SET_SCREEN, APP_OBJ, 0x29, 0, 0)
    pump()
    scr, page3 = where()
    res.check(scr == 0x29 and b.u8(page3 + 0xA5E0) == 4, f"screen 0x29 shows Master ({scr:#x})")
    if "dj_tools_tick" in bench.sym:            # 3.1.T: Tools from elsewhere, then the FX row's Master
        b.call(bench.sym["deck_init"])
        b.call(SET_SCREEN, APP_OBJ, 0x25, 0, 0)
        pump()
        scr, page4 = where()
        res.check(scr == 0x25 and b.u8(page4 + 0xA5E0) == 0, f"Tools from the menu: Main, not the leftover Master ({b.u8(page4 + 0xA5E0)})")
        rows.clear()
        b.call(SET_SCREEN, APP_OBJ, 0x29, 0, 0)
        pump()
        scr, page5 = where()
        res.check(scr == 0x29 and b.u8(page5 + 0xA5E0) == 4 and rows[-8:] == want,
                  f"Master from Main: the compressor list ({b.u8(page5 + 0xA5E0)}, {[hex(i) for i, _ in rows[-8:]]})")


# ---- Warp Rubber (3.1.R): pairing, lane hook, card loader, engine ---------------------------

RB_VOICE = 0x38007000
RB_B2 = 0x08100000


def leaf_v(bench):
    """The bank-2 leaf's Warp value: 1 on Stretch / leaf, 2 on Stretch / Repitch / leaf (3.1.B
    --with-repitch). 3.1.R's cave predates the warp_leaf_v word: 1."""
    if "warp_leaf_v" in bench.sym:
        return bench.b.u32(bench.sym["warp_leaf_v"])
    return 1
# The stock lane's envelope, aux and filter resets (the leaf runs them as the stock lane render does;
# tools/stretch/bungee/bungee.cpp). In the render cases: unity gain, nothing to reset.
POSTS = []   # engine outbox messages the leaf posted (pad feedback), 24 bytes each


def _post(bb):
    POSTS.append(bytes(bb.mu.mem_read(bb.arg(1), 24)))


def _tick_at(bb):
    bb.mu.mem_write(bb.arg(0), struct.pack("<q", 12345))


def _level_gain(bb):
    """FUN_08060880 (normalised Level -> gain) in Python: it computes in doubles, which Unicorn's
    Cortex-M7 cannot run. 0 at or below 0, else 10^(min(12, 64 (x - 1)) / 20)."""
    x = struct.unpack("<f", struct.pack("<I", bb.mu.reg_read(bd.A.UC_ARM_REG_S0)))[0]
    g = 0.0 if x <= 0 else 10 ** (min(12.0, 64.0 * (x - 1.0)) / 20.0)
    bb.mu.reg_write(bd.A.UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", g))[0])


LANE_STUBS = ((0x0806A6C0, lambda bb: 0), (0x0806A63C, lambda bb: 0), (0x0806A698, lambda bb: 0),
              (0x0806A5D8, lambda bb: 0), (0x0806EF38, lambda bb: 0), (0x08076704, lambda bb: 0),
              (0x0804F504, _post), (0x08069C44, _tick_at), (0x08060880, _level_gain))
def clip_level(b, clip, norm=1.0):
    """Wire both lanes' Level param object (lane+0x58) the way the Clip ctor and a 0 dB Level do:
    one source, the base value lane+0x90 at amount 1, clamped to 0..1.1875. The leaf reads it with
    stock FUN_0806FB30, which returns 0 for an object without sources (a zeroed bench Clip)."""
    for lane in (clip + 0x260, clip + 0x670):
        lane_level(b, lane, norm)


def lane_level(b, lane, norm=1.0):
    """clip_level for one lane (after a fixture zeroes the lane)."""
    if True:
        obj = lane + 0x58
        b.w32(obj, lane + 0x90 - 4)             # source 0: value at ptr + 4 = lane+0x90
        b.mu.mem_write(obj + 4, struct.pack("<f", 1.0))
        b.mu.mem_write(obj + 0x20, b"\x00")
        b.w32(obj + 0x24, 1)
        b.mu.mem_write(obj + 0x28, struct.pack("<ff", 0.0, 1.1875))
        b.mu.mem_write(lane + 0x90, struct.pack("<f", norm))


RB_BKP = 0x38800000
FLASH2_REGS = (0x52002104, 0x5200210C, 0x52002110, 0x52002114)   # KEYR2 CR2 SR2 CCR2
FLASH1_REGS = (0x52002004, 0x5200200C, 0x52002010, 0x52002014)   # KEYR1 CR1 SR1 CCR1


def rb_leaf(bench):
    """Which bank-2 leaf this image was built with: ("Bungee", "BBBUNGEE") for 3.1.B, else Warp
    Rubber's ("Rubber", "BBRUBBER"). Same machinery, so the same tests; the names differ."""
    img = Path(bench.image_path)
    if img.with_name(img.name.replace("BLACKBOX", "BBWARP")).exists():
        return "Bungee", "BBWARP"             # 3.1.T: Bungee plus Fill / Chop / Grain
    if img.with_name(img.name.replace("BLACKBOX", "BBBUNGEE")).exists():
        return "Bungee", "BBBUNGEE"
    return "Rubber", "BBRUBBER"


def rb_name(bench):
    return rb_leaf(bench)[0]


def rb_file(bench):
    return rb_leaf(bench)[1] + ".BIN"


def rb_payload(bench):
    """The bank-2 payload built with this image (BBRUBBER.BIN / BBBUNGEE.BIN, or the _emu one, beside it)."""
    img = Path(bench.image_path)
    p = img.with_name(img.name.replace("BLACKBOX", rb_leaf(bench)[1]))
    return bytearray(p.read_bytes()) if p.exists() else None


def rb_state(bench):
    return bench.b.u32(RB_VOICE + 8) & 0xFF


def rb_label(bench):
    return bench.b.cstr(RB_VOICE + 32)


def rb_bkp(bench, mark=0, booting=0):
    b = bench.b
    b.w32(RB_BKP, 0x4B424252)      # "RBBK": rb_bkp_enable keeps the words
    b.w32(RB_BKP + 4, mark)
    b.w32(RB_BKP + 16, booting)


def t_rubber_absent(bench, res):
    """Erased bank 2: Rubber is listed, says "Rubber!", and never renders."""
    b = bench.b
    b.call(bench.sym["state_init"])
    res.check(rb_state(bench) == 2, f"voice state {rb_state(bench)} (2 = absent)")
    res.check(rb_label(bench) == rb_name(bench) + "!", f"label {rb_label(bench)!r}")
    res.check(b.call(bench.sym["rb_ready"]) == 0, "rb_ready is 0")
    labels = bench.sym["labels_repitch"]
    if leaf_v(bench) == 5:                      # 3.1.T
        want = ["Stutter", "Fill", "Repitch", "Chop", "Grain", rb_name(bench) + "!"]
    else:
        want = ["Stretch"] + (["Repitch"] if leaf_v(bench) == 2 else []) + [rb_name(bench) + "!"]
    got = [b.cstr(b.u32(labels + 4 * i)) for i in range(len(want))]
    res.check(got == want, f"Warp labels {got}")
    reg = bench.sym["hook_register"] & ~1
    code = bytes(b.mu.mem_read(reg, 0x40))
    res.check(bytes([len(want), 0x24]) in code, f"Warp enum has {len(want)} values (movs r4, #{len(want)})")
    res.check(b.u32(0x24000088) == 0xC4000000, f"sample stack top {b.u32(0x24000088):#x} (all SDRAM for samples)")


def t_rubber_pair_rejects(bench, res):
    """A payload that is not exactly this image's, or a leftover scan mark, keeps Rubber off."""
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    boot = bench.sym["rb_pair"]
    reads = [0]

    def on_read(mu, access, addr, size, value, data):
        reads[0] += 1

    for what, mutate in (("one byte flipped", lambda p: p.__setitem__(len(p) // 2, p[len(p) // 2] ^ 1)),
                         ("wrong id", lambda p: p.__setitem__(4, p[4] ^ 0xFF))):
        p = bytearray(pay)
        mutate(p)
        b.mu.mem_write(RB_B2, bytes(p))
        rb_bkp(bench)
        b.call(boot)
        res.check(rb_state(bench) == 2 and rb_label(bench) == rb_name(bench) + "!", f"{what}: absent ({rb_state(bench)})")
    from unicorn import UC_HOOK_MEM_READ
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench, mark=0x4E435352)     # "RSCN": the last scan never came back
    h = b.mu.hook_add(UC_HOOK_MEM_READ, on_read, begin=RB_B2, end=RB_B2 + 0xFFFFF)
    b.call(boot)
    b.mu.hook_del(h)
    res.check(rb_state(bench) == 2, "scan mark: absent")
    res.check(reads[0] == 0, f"scan mark: bank 2 not read ({reads[0]} reads)")
    rb_bkp(bench, booting=0x544F4252)  # "RBOT": the last boot died in rubber_boot
    b.watch(bench.sym["rubber_boot"])
    b.call(boot)
    res.check(rb_state(bench) == 4 and rb_label(bench) == rb_name(bench) + "!!", f"boot mark: broken, off, {rb_label(bench)!r}")
    res.check(b.calls[bench.sym["rubber_boot"] & ~1] == 0, "boot mark: rubber_boot not entered")


def t_rubber_lane_hook(bench, res):
    """hook_lane renders a Rubber Clip once per block through bank 2's mix entry, only when ready."""
    b = bench.b
    calls, stock = [], [0]
    fake_mix = 0x08180001
    b.call(bench.sym["state_init"])
    head = bytes(4 * 5) + struct.pack("<I", fake_mix) + bytes(8)
    b.mu.mem_write(RB_B2, head)
    b.stub(fake_mix, lambda bb: calls.append((bb.arg(0), bb.arg(1), bb.arg(2), bb.arg(3))))
    b.stub(0x080667BC, lambda bb: stock.__setitem__(0, stock[0] + 1))
    clip, mixbuf, ctx = 0x2001C800, 0x2001E000, 0x2001E100
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    clip_level(b, clip)
    b.mu.mem_write(clip + 0xBB1, bytes([leaf_v(bench)]))   # the leaf's Warp value
    b.mu.mem_write(clip + 0x260, b"\x01")       # lane 0 active
    lane0, lane1 = clip + 0x260, clip + 0x670
    hook = bench.sym["hook_lane"]

    def lane_call(lane):
        try:
            b.call(hook, clip, 0, lane, 0, 0, mixbuf, ctx)
        except RuntimeError:
            pass                                # the stock stub returns with lane_stock's push on the stack

    lane_call(lane0)
    res.check(stock[0] == 1 and not calls, f"not ready: stock grains ({stock[0]}), no render ({len(calls)})")
    b.w32(RB_VOICE + 8, 1)                       # READY
    lane_call(lane0)
    res.check(calls == [(clip, lane0, mixbuf, ctx)], f"ready: one render with the right args {calls}")
    res.check(b.u32(RB_VOICE + 4) == clip, f"owner {b.u32(RB_VOICE + 4):#x}")
    lane_call(lane1)
    res.check(len(calls) == 1, "second lane in the same block does not render again")
    b.mu.mem_write(clip + 0x254, b"\x01")       # Slicer
    b.w32(RB_VOICE + 24, 0)
    lane_call(lane0)
    res.check(len(calls) == 1, "Slicer keeps stock grains")


class FlashModel:
    """STM32H7 bank 2 controller, enough for b2load.c: lock/keys, sector erase, 256-bit programming.
    Records every register and bank 2 write so the test can check what was touched."""

    def __init__(self, b, fail=False):
        from unicorn import UC_HOOK_MEM_WRITE
        self.b, self.fail = b, fail
        self.keys, self.erased, self.words, self.bad = [], [], [], []
        self.reset = False
        try:
            b.mu.mem_map(0x52000000, 0x10000)
        except Exception:
            pass                                # already mapped by an earlier model
        b.w32(0x5200210C, 1)                    # CR2: locked
        b.w32(0x52002110, 0)                    # SR2: idle, no errors
        self.h = [b.mu.hook_add(UC_HOOK_MEM_WRITE, self.on_reg, begin=0x52002000, end=0x520021FF),
                  b.mu.hook_add(UC_HOOK_MEM_WRITE, self.on_flash, begin=RB_B2, end=0x081FFFFF),
                  b.mu.hook_add(UC_HOOK_MEM_WRITE, self.on_aircr, begin=0xE000ED0C, end=0xE000ED0F)]

    def close(self):
        for h in self.h:
            self.b.mu.hook_del(h)

    def on_reg(self, mu, access, addr, size, value, data):
        if addr in FLASH1_REGS:
            self.bad.append(f"bank 1 register {addr:#x}")
        if addr == 0x52002104:
            self.keys.append(value)
            if self.keys[-2:] == [0x45670123, 0xCDEF89AB]:
                mu.mem_write(0x5200210C, struct.pack("<I", 0))
        elif addr == 0x5200210C and (value & 0x84) == 0x84:    # SER | START
            sector = (value >> 8) & 7
            self.erased.append(sector)
            self.sectors6 = getattr(self, "sectors6", 0) + (sector == 6)
            if sector >= 7:
                self.bad.append(f"erase of sector {sector}")
            mu.mem_write(RB_B2 + sector * 0x20000, b"\xff" * 0x20000)
            if self.fail:
                mu.mem_write(0x52002110, struct.pack("<I", 1 << 17))   # WRPERR

    def on_flash(self, mu, access, addr, size, value, data):
        off = addr - RB_B2
        if not (off < 0xC0000 or 0xC0000 <= off < 0xC0020):
            self.bad.append(f"write at bank 2 +{off:#x}")
        self.words.append(off & ~31)

    def on_aircr(self, mu, access, addr, size, value, data):
        if value & 4:
            self.reset = True
            mu.emu_stop()


class FakeFile:
    """f_open/f_read/f_close serving one file from memory."""

    def __init__(self, b, name, data, others=()):
        self.b, self.name, self.data, self.pos, self.opens = b, name, data, 0, 0
        self.others = set(others)
        b.stub(0x08086534, self.f_open)
        b.stub(0x08086866, self.f_read)
        b.stub(0x08086D76, lambda bb: 0)

    def f_open(self, b):
        if b.cstr(b.arg(1)) in self.others:
            return 0
        if b.cstr(b.arg(1)) != self.name or self.data is None:
            return 4                            # FR_NO_FILE
        self.pos, self.opens = 0, self.opens + 1
        return 0

    def f_read(self, b):
        to, n, got = b.arg(1), b.arg(2), b.arg(3)
        chunk = bytes(self.data[self.pos:self.pos + n])
        b.mu.mem_write(to, chunk)
        b.w32(got, len(chunk))
        self.pos += len(chunk)
        return 0


def rb_loader_boot(bench):
    """A boot with an erased bank 2 as far as the loader sees: pairing says absent, card is up."""
    b = bench.b
    b.call(bench.sym["rb_pair"])
    b.mu.mem_write(0x240021A4 + 0x15, b"\x01")


def rb_poll(bench):
    try:
        bench.b.call(bench.sym["rb_load_poll"], limit=400_000_000)
    except RuntimeError:
        pass                                    # a requested reset stops the emulation


def t_rubber_loader(bench, res):
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    rb_bkp(bench)
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), pay)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    got = bytes(b.mu.mem_read(RB_B2, len(pay)))
    res.check(got == bytes(pay), "bank 2 holds the payload")
    res.check(b.u32(RB_B2 + 0xC0000) == 0x43424252, "claim word written at sector 6")
    res.check(fm.erased == list(range((len(pay) + 0x1FFFF) // 0x20000)), f"erased sectors {fm.erased}")
    res.check(not getattr(fm, "sectors6", 0), "claim sector 6 not erased on a blank bank")
    res.check(not fm.bad, f"nothing outside bank 2's payload/claim or registers {fm.bad[:3]}")
    hdr = [i for i, w in enumerate(fm.words) if w == 0]
    res.check(hdr and hdr[0] > max(i for i, w in enumerate(fm.words) if w != 0 and w != 0xC0000),
              "header flash word written last")
    res.check(fm.reset, "restart requested after a verified write")
    res.check(b.u32(RB_BKP + 12) == 0, "tries counter cleared")
    # A second poll in the same boot does nothing.
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased and not fm.words, "one go per boot")


def t_rubber_loader_refuses(bench, res):
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    # Foreign data in bank 2 and no claim: left alone.
    b.mu.mem_write(RB_B2 + 0x50000, b"\x12\x34\x56\x78")
    rb_bkp(bench)
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), pay)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased and not fm.words, "foreign bank 2: no erase, no write")
    res.check(b.u32(RB_B2 + 0x50000) == 0x78563412, "foreign data untouched")
    res.check(rb_label(bench) == rb_name(bench) + "! x", f"foreign bank 2 named: {rb_label(bench)!r}")
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), None)
    rb_poll(bench)
    res.check(rb_label(bench) == rb_name(bench) + "! -", f"no file named: {rb_label(bench)!r}")
    # A damaged file is never written.
    b.mu.mem_write(RB_B2, b"\xff" * 0x100000)
    bad = bytearray(pay)
    bad[len(bad) // 3] ^= 0x40
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), bad)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased, "damaged file: nothing erased")
    res.check(rb_label(bench) == rb_name(bench) + "! ?", f"damaged file named: {rb_label(bench)!r}")
    # Failing flash: two tries, then no more.
    b.mu.mem_write(RB_B2, b"\xff" * 0x100000)
    attempts = []
    for _ in range(3):
        rb_loader_boot(bench)
        FakeFile(b, rb_file(bench), pay)
        fm = FlashModel(b, fail=True)
        rb_poll(bench)
        fm.close()
        attempts.append(len(fm.erased))
    res.check(attempts[0] > 0 and attempts[1] > 0 and attempts[2] == 0, f"tries per boot {attempts}")
    res.check(rb_label(bench) == rb_name(bench) + "! t", f"tries used up: {rb_label(bench)!r}")


def t_rubber_engine(bench, res):
    """--emu build: bank 2 boots, pairs, and renders a Clip through stubbed stock buffers."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path:
        res.check(True, "engine run needs the --emu build (skipped)")
        return
    import math
    b = bench.b
    # Bank 2 and ITCM code executed, in bytes, for the mono / stereo comparison. Added before
    # anything runs: Unicorn does not call a block hook for code it translated before the hook.
    from unicorn import UC_HOOK_BLOCK
    work = [0]
    b.mu.hook_add(UC_HOOK_BLOCK, lambda mu, a, sz, d: work.__setitem__(0, work[0] + sz), begin=RB_B2, end=0x081FFFFF)
    b.mu.hook_add(UC_HOOK_BLOCK, lambda mu, a, sz, d: work.__setitem__(0, work[0] + sz), begin=0x1000, end=0xFFFF)
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    res.check(rb_state(bench) == 3 and rb_label(bench) == rb_name(bench) + "..", f"paired, not started: {rb_label(bench)!r}")
    sp = []
    from unicorn import UC_HOOK_CODE
    h = b.mu.hook_add(UC_HOOK_CODE, lambda mu, a, sz, d: sp.append(mu.reg_read(bd.A.UC_ARM_REG_SP)),
                      begin=bench.sym["rubber_boot"] & ~1, end=bench.sym["rubber_boot"] & ~1)
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    b.mu.hook_del(h)
    res.check(rb_state(bench) == 1 and rb_label(bench) == rb_name(bench) + "", f"started from the disk poll: {rb_label(bench)!r}")
    res.check(sp and 0x30040000 <= sp[0] <= 0x30048000, f"rubber_boot runs on Rubber's own stack ({sp[0]:#x})" if sp else "rubber_boot ran")
    res.check(b.u32(RB_BKP + 16) == 0 and b.u32(RB_BKP + 20) == 0, "boot mark and stage cleared after the start")
    if rb_state(bench) != 1:
        return
    sr, n_src = 48000, 48000 * 2
    src = [0.4 * math.sin(2 * math.pi * 220 * i / sr) * (1.0 if (i // 6000) % 2 == 0 else 0.3) for i in range(n_src)]
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    stereo = [1]
    planes = (0x2001E400, 0x2001E800)          # not in SRAM1-3: that is the leaf's heap
    out = []

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[src[(pos + i) % n_src] for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    def buf_mix(bb):
        out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: stereo[0]),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, buf_mix),
                     *LANE_STUBS):
        b.stub(addr, fn)
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    clip_level(b, clip)
    b.w32(clip + 0x24, sample)
    b.w32(ctx, 0)                               # no transport: no clock lock
    b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(RB_VOICE + 20, 1)                     # launch
    if "rb_work" in bench.sym:                  # stack depth of each side: paint, render, look
        b.mu.mem_write(0x30045000, b"\xa5" * 0x3000)
    for _ in range(60):
        b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=400_000_000)
        if "rb_work" in bench.sym:              # the leaf task's wake, after the block
            b.call(bench.sym["rb_work"], limit=400_000_000)
    if os.environ.get("RB_DUMP"):                 # a render for comparing two builds (bungee README)
        Path(os.environ["RB_DUMP"]).write_text("\n".join(f"{x:.9g}" for x in out))
    finite = all(math.isfinite(x) for x in out)
    silent = next((i for i, x in enumerate(out) if abs(x) > 1e-4), len(out))
    tail = out[20 * 256:]
    rms = math.sqrt(sum(x * x for x in tail) / max(1, len(tail)))
    res.check(len(out) == 60 * 256 and finite, f"60 blocks rendered, all finite ({len(out)})")
    res.check(silent < 20 * 256, f"sound after the priming blocks (first at {silent} samples)")
    res.check(0.08 < rms < 0.5, f"output level {rms:.3f} (input 0.2-0.28 rms)")
    ups = sum(1 for i in range(1, len(tail)) if tail[i - 1] < 0 <= tail[i])
    hz = ups * sr / max(1, len(tail))
    res.check(205 < hz < 235, f"pitch kept at 0.8x tempo: {hz:.0f} Hz (input 220 Hz)")
    used = b.u32(bench.sym["rb_fast_used"])
    limit = 0x30045000 - 0x30000000 + (0x10000 if rb_name(bench) == "Bungee" else 0)   # + ITCM pool
    res.check(0 < used <= limit and not b.u32(bench.sym["rb_heap_failed"]),
              f"heap all in internal RAM ({used} bytes), nothing in SDRAM")
    if "rb_work" in bench.sym:
        st = bytes(b.mu.mem_read(0x30045000, 0x3000))
        task_low = next((k for k in range(0, 0x2000) if st[k] != 0xa5), 0x2000)
        audio_low = next((k for k in range(0x2000, 0x3000) if st[k] != 0xa5), 0x3000)
        task_used, audio_used = 0x2000 - task_low, 0x3000 - audio_low
        res.check(task_used <= 0x1800 and audio_used <= 0xC00,
                  f"stacks: leaf task {task_used} of 8192 bytes, audio side {audio_used} of 4096")
    if rb_name(bench) == "Bungee":
        # 3.1.B: a launch plays the Clip itself for the 8 priming blocks, then crossfades into
        # Bungee (tools/stretch/bungee/bungee.cpp); the heap must leave room below the stack.
        raw = struct.unpack("<2048f", struct.pack("<2048f", *src[:2048]))
        res.check(max(abs(x - y) for x, y in zip(out[:2048], raw)) == 0.0, "launch plays the Clip itself for 2048 frames")
        peak = b.u32(bench.sym["rb_fast_peak"])
        heap0 = (bench.sym["__b2_bss_end"] + 15) & ~15
        free = 0x30045000 - heap0 - peak
        itcm = b.u32(bench.sym["rb_itcm_used"])
        itcm_free = 0x10000 - ((bench.sym["__itcm_end"] + 15) & ~15) - itcm
        res.check(0 < peak and free >= 0 and itcm_free >= 4096,
                  f"heap {peak} bytes from {heap0:#x} ({free} left below the stack) + {itcm} in ITCM "
                  f"({itcm_free} left)")
        # A mono Clip: the next launch rebuilds the stretcher on one channel (heap rewound to its
        # boot mark), plays the Clip raw one sub-block longer, and costs less per block.

        def blocks(k):
            cost = []
            for _ in range(k):
                work[0] = 0
                b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=400_000_000)
                if "rb_work" in bench.sym:              # the leaf task's wake, after the block
                    b.call(bench.sym["rb_work"], limit=400_000_000)
                cost.append(work[0])
            return cost
        stereo_cost = blocks(20)
        stereo[0] = 0
        out.clear()
        b.w32(RB_VOICE + 20, 1)                 # launch the (now mono) Clip
        mono_cost = blocks(60)
        raw = struct.unpack("<2048f", struct.pack("<2048f", *src[:2048]))
        res.check(rb_label(bench) == "Bungee" and not b.u32(bench.sym["rb_heap_failed"]),
                  f"mono: stretcher rebuilt on one channel ({rb_label(bench)!r})")
        res.check(max(abs(x - y) for x, y in zip(out[:2048], raw)) == 0.0, "mono: launch plays the Clip itself for 2048 frames")
        tail = out[20 * 256:]
        ups = sum(1 for i in range(1, len(tail)) if tail[i - 1] < 0 <= tail[i])
        hz = ups * sr / max(1, len(tail))
        res.check(205 < hz < 235 and all(math.isfinite(x) for x in out), f"mono: pitch kept at 0.8x, {hz:.0f} Hz")
        sc, mc = sum(stereo_cost) / len(stereo_cost), sum(mono_cost[20:]) / len(mono_cost[20:])
        res.check(mc < 0.7 * sc, f"mono: {mc / 1e6:.2f}M per block against stereo {sc / 1e6:.2f}M")
    n_itcm = bench.sym["__itcm_end"] - bench.sym["__itcm_start"]
    load = RB_B2 + 32                           # .itcm's image follows the 32-byte header (rubber.ld)
    res.check(n_itcm > 8192 and bytes(b.mu.mem_read(bench.sym["__itcm_start"], n_itcm)) == bytes(b.mu.mem_read(load, n_itcm)),
              f"hot code copied into ITCM ({n_itcm} bytes)")



def t_bungee_voice_end(bench, res):
    """3.1.B: the leaf does what the stock lane render does around its grains, so a Bungee Clip
    stops: amp envelope every block, end of voice when it finishes (STOP, note-off, mute arm it),
    the quantised release, stopped grains, the launch pre-delay, one lane at a time."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or rb_name(bench) != "Bungee":
        res.check(True, "Bungee emu build only (skipped)")
        return
    import math
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"Bungee running ({rb_label(bench)!r})")
        return
    n_src = 48000
    src = [0.3 * math.sin(2 * math.pi * 220 * i / 48000) for i in range(n_src)]
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    lane_a, lane_b = clip + 0x260, clip + 0x670
    out, calls = [], []

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[src[(pos + i) % n_src] for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     (0x0806A6C0, lambda bb: calls.append(("env", bb.arg(0)))),
                     (0x0806A63C, lambda bb: calls.append(("release", bb.arg(0)))),
                     (0x0806A698, lambda bb: calls.append(("fast", bb.arg(0)))),
                     (0x0806A5D8, lambda bb: calls.append(("env_reset", bb.arg(0)))),
                     (0x0806EF38, lambda bb: calls.append(("aux_reset", bb.arg(0)))),
                     (0x08076704, lambda bb: calls.append(("filt_reset", bb.arg(0)))),
                     (0x0804F504, _post), (0x08069C44, _tick_at), (0x08060880, _level_gain)):
        b.stub(addr, fn)

    def launch(lane, tick, predelay=0):
        b.mu.mem_write(lane, b"\x00" * 0x260)
        lane_level(b, lane)
        b.mu.mem_write(lane, b"\x01")             # active
        b.w32(lane + 0xD8, 1)                        # one stock grain in the list: slot 2
        b.w32(lane + 0xC8, 2)
        for o in (0xCC, 0xD0, 0xD4):
            b.w32(lane + o, 0xFFFFFFFF)
        b.mu.mem_write(clip + 2 * 0x70 + 0x30, b"\x01\x00")
        b.mu.mem_write(lane + 0xE0, struct.pack("<q", tick))
        b.w32(lane + 0x138, predelay)
        b.mu.mem_write(lane + 0x25C, b"\x03")       # envelope in sustain
        b.w32(RB_VOICE + 20, 1)                      # the grain hook's reset request

    def mix(k):
        for _ in range(k):
            b.call(bench.sym["rb_mix"], clip, lane_a, mixbuf, ctx, limit=400_000_000)
            if "rb_work" in bench.sym:              # the leaf task's wake, after the block
                b.call(bench.sym["rb_work"], limit=400_000_000)

    b.mu.mem_write(clip, b"\x00" * 0xC00)
    clip_level(b, clip)
    b.w32(clip + 0x24, sample)
    b.w32(ctx, 0)                               # no transport: no clock lock
    b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
    launch(lane_a, 1000, predelay=100)
    mix(12)
    raw = struct.unpack("<1948f", struct.pack("<1948f", *src[:1948]))
    res.check(not any(out[:100]) and max(abs(x - y) for x, y in zip(out[100:2048], raw)) == 0.0,
              "pre-delay: 100 silent frames, then the raw launch from frame 0")
    envs = [a for n_, a in calls if n_ == "env"]
    res.check(len(envs) >= 11 and all(a == lane_a + 0x258 for a in envs), f"amp envelope every block ({len(envs)})")
    # A quantised release: 600 frames to go, released in the block where fewer than 256 remain.
    calls.clear()
    b.mu.mem_write(lane_a + 0x134, b"\x01\x00")
    b.w32(lane_a + 0x130, 600)
    mix(2)
    res.check([c for c in calls if c[0] == "release"] == [], "release not before it is due")
    mix(1)
    rel = [c for c in calls if c[0] in ("release", "fast")]
    res.check(len(rel) == 2 and rel[0][1] == lane_a + 0x258 and b.u8(lane_a + 0x134) == 0,
              f"release when due, flag cleared ({rel})")
    # The envelope finished (STOP / note-off / mute): the lane ends as stock ends it.
    calls.clear()
    b.mu.mem_write(lane_a + 0x25C, b"\x05")
    mix(1)
    names = [c[0] for c in calls]
    res.check(b.u8(lane_a) == 0 and b.u32(lane_a + 0xD8) == 0 and b.u32(lane_a + 0xC8) == 0xFFFFFFFF,
              "envelope done: lane off, grain list empty")
    res.check(b.u16(clip + 2 * 0x70 + 0x30) == 0, "its stock grain slot freed")
    res.check(("env_reset", lane_a + 0x258) in calls and ("aux_reset", lane_a + 0xF0) in calls
              and ("filt_reset", lane_a + 0x14C) in calls, f"envelope, aux and filter reset ({names})")
    res.check(rb_label(bench) == "Bungee", f"label back to plain ({rb_label(bench)!r})")
    k = len(out)
    mix(3)
    res.check(len(out) == k, "nothing mixed once the voice ended")
    # Grains stopped (Clip_StopGrains: lane[9], empty list): ends too.
    launch(lane_a, 2000)
    mix(4)
    b.mu.mem_write(lane_a + 9, b"\x01")
    b.w32(lane_a + 0xD8, 0)
    mix(1)
    res.check(b.u8(lane_a) == 0, "grains stopped: lane off")
    # A relaunch on the other lane while the first still sounds: the old lane ends, the new renders.
    launch(lane_a, 3000)
    mix(3)
    calls.clear()
    launch(lane_b, 4000)
    mix(2)
    res.check(b.u8(lane_a) == 0 and b.u8(lane_b) == 1, "relaunch: old lane ended, new lane on")
    envs = [a for n_, a in calls if n_ == "env"]
    res.check(envs and all(a == lane_b + 0x258 for a in envs), "relaunch: the new lane's envelope runs")


def t_bungee_clock_feedback_switch(bench, res):
    """3.1.B: pad feedback (0x4A playhead every block, 0x1D after a quantised delay), the clock
    lock against the transport's tempo map, latest-launch-wins pad switches, and loop off."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or rb_name(bench) != "Bungee":
        res.check(True, "Bungee emu build only (skipped)")
        return
    import math
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"Bungee running ({rb_label(bench)!r})")
        return
    n_src = 48000 * 8
    srcs = {}
    clip_a, clip_b = 0x2001C800, 0x2001A000
    sample_a, sample_b = 0x2001C000, 0x2001C400
    srcs[sample_a] = lambda i: 0.3 * math.sin(2 * math.pi * 220 * i / 48000)
    srcs[sample_b] = lambda i: 0.3 * math.sin(2 * math.pi * 330 * i / 48000)
    lens = {sample_a: n_src, sample_b: n_src}
    mixbuf, ctx, scratch = 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    tp = 0x2001EC00                                  # a fake transport
    out = []

    def pcm_read(bb):
        smp, dest, count, pos = bb.arg(0), bb.arg(1), bb.arg(2), bb.arg(3)
        f = srcs[smp]
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[f((pos + i) % lens[smp]) for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: lens[bb.arg(0)]), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)

    bpm, ratio = 120.0, 0.8
    clip_ticks = int(round(n_src * bpm / (3000 * ratio)))   # ratio = frames / ticks * BPM / 3000

    def setup_clip(clip, sample, wave):
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        clip_level(b, clip)
        b.w32(clip + 0x18, 0x100 + wave)              # pad
        b.w32(clip + 0x24, sample)
        b.w32(sample + 0x24, wave)                    # wave id
        b.w32(clip + 0x248, 0xFFFFFFFF)
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", ratio))
        b.mu.mem_write(clip + 0xBA8, struct.pack("<q", clip_ticks))
        b.mu.mem_write(clip + 0x1F4, b"\x01")        # loop on
        b.mu.mem_write(clip + 0x1F8, b"\x00")

    def launch(clip, lane, now, predelay=0, grain_flag=0):
        b.mu.mem_write(lane, b"\x00" * 0x260)
        lane_level(b, lane)
        b.mu.mem_write(lane, b"\x01")
        b.w32(lane + 0xD8, 1)
        b.w32(lane + 0xC8, 0)
        for o in (0xCC, 0xD0, 0xD4):
            b.w32(lane + o, 0xFFFFFFFF)
        b.mu.mem_write(clip + 0x30, b"\x01\x00")
        b.mu.mem_write(clip + 0x6C, bytes([grain_flag]))
        b.mu.mem_write(lane + 0xE0, struct.pack("<q", now))
        b.w32(lane + 0x138, predelay)
        b.mu.mem_write(lane + 0x25C, b"\x03")
        b.w32(RB_VOICE + 20, 1)

    now = [0]

    def set_tp(bpm_now, playing=1):
        b.mu.mem_write(tp, struct.pack("<qqqf", now[0], 0, 0, bpm_now))
        b.mu.mem_write(tp + 0x38, bytes([playing]))

    def mix(clip, k, bpm_now=bpm):
        for _ in range(k):
            set_tp(bpm_now)
            b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=400_000_000)
            b.call(bench.sym["rb_work"], limit=400_000_000)
            now[0] += 256

    setup_clip(clip_a, sample_a, 7)
    setup_clip(clip_b, sample_b, 8)
    b.w32(ctx, tp)
    POSTS.clear()
    launch(clip_a, clip_a + 0x260, now[0], predelay=100, grain_flag=1)
    mix(clip_a, 30)
    ups = [m for m in POSTS if m[0] == 0x4A]
    started = [m for m in POSTS if m[0] == 0x1D]
    pos = [struct.unpack_from("<I", m, 0x10)[0] for m in ups]
    res.check(len(started) == 1 and struct.unpack_from("<I", started[0], 8)[0] == 0x107,
              f"0x1D once after the launch delay ({len(started)})")
    res.check(len(ups) >= 29 and all(q > p for p, q in zip(pos, pos[1:])) and
              struct.unpack_from("<I", ups[-1], 0xC)[0] == 7, f"0x4A every block, rising ({len(ups)}, last {pos[-1] if pos else None})")
    pm = struct.unpack_from("<h", ups[-1], 0x14)[0]
    res.check(abs(pm - pos[-1] * 1000 // n_src) <= 1, f"0x4A permille {pm}")

    # The clock runs 0.3 % fast against the Clip's ratio: Bungee pulls in and stays locked.
    fast = bpm * 1.003

    def expected():
        tick = now[0] * fast / 3000.0
        return (tick - anchor) * n_src / clip_ticks

    POSTS.clear()
    anchor = 0.0
    launch(clip_a, clip_a + 0x260, now[0])
    anchor = now[0] * fast / 3000.0
    errs = []
    for _ in range(8):
        mix(clip_a, 100, bpm_now=fast)
        ups = [struct.unpack_from("<I", m, 0x10)[0] for m in POSTS if m[0] == 0x4A]
        errs.append((ups[-1] - expected()) if ups else 1e9)
    free = 0.003 * ratio * 800 * 256                 # how far it would be off unlocked
    res.check(abs(errs[-1]) < 96 and abs(errs[-2]) < 96,
              f"clock lock: {errs[-1]:.0f} frames off after ~4 s at +0.3 % (unlocked: {free:.0f}); "
              f"track {[round(e) for e in errs]}")

    # Latest launch wins: B takes the voice while A plays; A's lanes end, B renders at once.
    launch(clip_b, clip_b + 0x260, now[0])
    b.w32(RB_VOICE + 72, clip_a)                    # what hook_main_rate records on the steal
    b.w32(RB_VOICE + 4, clip_b)
    out.clear()
    mix(clip_b, 2)
    res.check(b.u8(clip_a + 0x260) == 0, "switch: the previous Clip's lane ended")
    want = [srcs[sample_b](i) for i in range(256)]
    res.check(max(abs(x - y) for x, y in zip(out[:256], want)) < 1e-6, "switch: the new Clip plays from its first frame at once")
    res.check(b.u32(RB_VOICE + 72) == 0, "switch: record cleared")

    # Loop off: the voice ends at the end of the file.
    lens[sample_b] = 6000
    b.mu.mem_write(clip_b + 0x1F4, b"\x00")
    launch(clip_b, clip_b + 0x260, now[0])
    mix(clip_b, 40)
    res.check(b.u8(clip_b + 0x260) == 0, "loop off: lane ended at the end of the file")


def t_bungee_pitch_level(bench, res):
    """3.1.T Bungee: the Clip's Pitch shifts the pitch at the same tempo (-12..+7 st), and the pad's
    Level applies (lane.h lane_gain)."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or rb_name(bench) != "Bungee" or "str_stutter" not in bench.sym:
        res.check(True, "3.1.T emu build only (skipped)")
        return
    import math
    b = bench.b
    b.stub(0x0804436C, lambda bb: 0xC3000000)
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"Bungee running ({rb_label(bench)!r})")
        return
    sr, n_src, period = 48000, 48000 * 4, 6000
    src = [0.4 * math.sin(2 * math.pi * 220 * i / sr) * (1.0 if (i // period) % 2 == 0 else 0.3) for i in range(n_src)]
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    out = []

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[src[(pos + i) % n_src] for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)

    def render(milli_st, level=1.0, blocks=100):
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        clip_level(b, clip, level)
        b.w32(clip + 0x24, sample)
        b.w32(ctx, 0)
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
        b.mu.mem_write(clip + 0xA80, struct.pack("<i", milli_st))
        b.mu.mem_write(clip + 0x260, b"\x01")
        b.w32(RB_VOICE + 20, 1)
        out.clear()
        for _ in range(blocks):
            b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=400_000_000)
            b.call(bench.sym["rb_work"], limit=400_000_000)
        return out[24 * 256:]

    def hz(xs):
        return sum(1 for i in range(1, len(xs)) if xs[i - 1] < 0 <= xs[i]) * sr / max(1, len(xs))

    def steps(xs):
        """Output frames between the source's level steps (every 6000 source frames)."""
        w = [math.sqrt(sum(x * x for x in xs[i:i + 250]) / 250) for i in range(0, len(xs) - 249, 250)]
        hi, lo = max(w), min(w)
        mid = (hi + lo) / 2
        edges = [i * 250 for i in range(1, len(w)) if (w[i] > mid) != (w[i - 1] > mid)]
        d = [q - p for p, q in zip(edges, edges[1:])]
        return sum(d) / len(d) if d else 0

    for st, want in ((0, 220.0), (7000, 220.0 * 2 ** (7 / 12)), (-12000, 110.0)):
        tail = render(st)
        f, gap = hz(tail), steps(tail)
        res.check(abs(f / want - 1) < 0.05 and abs(gap / (period / 0.8) - 1) < 0.08 and all(math.isfinite(x) for x in tail),
                  f"Pitch {st / 1000:+.0f} st at 0.8x: {f:.0f} Hz (want {want:.0f}), level steps every {gap:.0f} frames (want 7500)")
    ref = render(0)
    quiet = render(0, level=1.0 - 6.0 / 64)
    rr = math.sqrt(sum(x * x for x in quiet) / len(quiet)) / math.sqrt(sum(x * x for x in ref) / len(ref))
    res.check(abs(rr - 10 ** (-6 / 20)) < 0.03, f"Bungee follows the pad's Level: -6 dB -> x{rr:.3f}")
    b.unstub(0x0804436C)


def t_lineup_deck(bench, res):
    """3.1.T with Clip Deck: Fill, Grain and Bungee follow the Clip's Loop Start / Loop End (deck.c
    steers the unit sequence through lane.h DeckSeq): units 0..7, then 4..7 again."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or "str_stutter" not in bench.sym or "deck_init" not in bench.sym:
        res.check(True, "3.1.T + Clip Deck emu build only (skipped)")
        return
    import math
    b = bench.b
    b.stub(0x0804436C, lambda bb: 0xC3000000)
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    b.call(bench.sym["deck_init"])
    if rb_state(bench) != 1:
        res.check(False, f"bank 2 running ({rb_label(bench)!r})")
        return
    spu, units = 6000, 32
    n_src = spu * units
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    tp = 0x2001EC00
    out = []

    def amp(k):
        return 0.05 + 0.02 * k

    def src(i):
        return amp((i % n_src) // spu) * math.sin(2 * math.pi * 440 * i / 48000)

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[src(pos + i) for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)
    now = [0]
    b.w32(ctx, tp)

    def setup(mode):
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        clip_level(b, clip)
        b.w32(clip + 0x18, 0x0100)                        # row 1, col 0 (deck pad 4)
        b.w32(clip + 0x24, sample)
        b.w32(sample + 0x24, 7)
        b.w32(clip + 0x248, 0xFFFFFFFF)
        b.mu.mem_write(clip + 0xBB1, bytes([mode]))
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
        b.mu.mem_write(clip + 0xBA8, struct.pack("<q", units * 240))
        b.mu.mem_write(clip + 0x1F4, b"\x01\x01")         # loop on, Sync on
        b.mu.mem_write(clip + 0x1F8, b"\x02")
        b.w32(clip + 0x208, 240)                          # 1/16 units
        b.w32(clip + 0x220, 240)
        b.w32(clip + 0x230, units * 240)
        b.w32(clip + 0x240, units)
        b.mu.mem_write(clip + 0x1FC, struct.pack("<f", float(spu)))
        for i, v in enumerate((0, POS_END, 4, 8)):        # Loop Start unit 4, Loop End unit 8
            b.call(bench.sym["deck_store_unpacked"], 0x0100, ID_DECK + i, v)
        lane = clip + 0x260
        b.mu.mem_write(lane, b"\x00" * 0x260)
        lane_level(b, lane)
        b.mu.mem_write(lane, b"\x01")
        b.w32(lane + 0xD8, 1)
        b.w32(lane + 0xC8, 0)                             # grain 0: its unit at clip+0x64
        b.w32(clip + 0x64, b.call(bench.sym["deck_note_unit"], clip, 0))   # stock's note start
        b.mu.mem_write(lane + 0xE0, struct.pack("<q", now[0] + 1))
        b.mu.mem_write(lane + 0x25C, b"\x03")
        return lane

    def run(fn, lane, blocks):
        out.clear()
        POSTS.clear()
        for _ in range(blocks):
            b.mu.mem_write(tp, struct.pack("<qqqf", now[0], 0, 0, 96.0))
            b.mu.mem_write(tp + 0x38, b"\x01")
            fn(lane)
            now[0] += 256

    unit_out = int(spu / 0.8)                             # 7500 output frames per unit at 96 BPM
    blocks = 10 * unit_out // 256 + 2

    def seq_from_level():
        ks = []
        for u in range(10):
            w = out[u * unit_out + 1000: (u + 1) * unit_out - 1000]
            r = math.sqrt(sum(x * x for x in w) / max(1, len(w)))
            ks.append(round((r * math.sqrt(2) - 0.05) / 0.02))
        return ks

    want = [0, 1, 2, 3, 4, 5, 6, 7, 4, 5]
    lane = setup(1)
    run(lambda ln: b.call(bench.sym["rb_lanes"], clip, ln, mixbuf, ctx, limit=400_000_000), lane, blocks)
    got = seq_from_level()
    res.check(got == want, f"Fill follows the loop: units {got} (want {want})")

    def last_pos():
        ups = [struct.unpack_from("<I", m, 0x10)[0] for m in POSTS if m[0] == 0x4A]
        return ups[-60:]

    lane = setup(4)
    run(lambda ln: b.call(bench.sym["rb_lanes"], clip, ln, mixbuf, ctx, limit=400_000_000), lane, blocks)
    tail = last_pos()
    res.check(tail and all(4 * spu - 600 <= p < 8 * spu + 600 for p in tail),
              f"Grain stays in the loop after Loop End (playhead {min(tail) if tail else None}..{max(tail) if tail else None})")

    def bungee(ln):
        b.call(bench.sym["rb_mix"], clip, ln, mixbuf, ctx, limit=400_000_000)
        b.call(bench.sym["rb_work"], limit=400_000_000)

    lane = setup(5)
    b.w32(RB_VOICE + 4, clip)
    b.w32(RB_VOICE + 20, 1)
    run(bungee, lane, blocks)
    tail = last_pos()
    res.check(tail and all(4 * spu - 600 <= p < 8 * spu + 600 for p in tail),
              f"Bungee relaunches into the loop at Loop End (playhead {min(tail) if tail else None}..{max(tail) if tail else None})")

    # A sequenced cue note one block after a bar boundary (it arrives after the bar's first unit was
    # chosen): Bungee still jumps at that bar, not a bar later. The launch is on a bar, as a quantised
    # launch is (a bar is 120000 frames at 96 BPM).
    now[0] = (now[0] // 120000 + 1) * 120000 - 1
    lane = setup(5)
    for i, v in enumerate((0, POS_END, 0, POS_END)):
        b.call(bench.sym["deck_store_unpacked"], 0x0100, ID_DECK + i, v)
    _cue_set(bench, 0x0100, cues=(7,))                   # Cue 1 at beat 6: unit 24
    b.w32(clip + 0x218, 3840)                            # Quant Size 1 bar (16 units)
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.w32(RB_VOICE + 4, clip)
    b.w32(RB_VOICE + 20, 1)
    out.clear()
    noted = False
    for _ in range(21 * unit_out // 256):
        b.mu.mem_write(tp, struct.pack("<qqqf", now[0], 0, 0, 96.0))
        b.mu.mem_write(tp + 0x38, b"\x01")
        bungee(lane)
        if not noted and len(out) >= 16 * unit_out + 256:  # one block past the bar at unit 16
            b.call(bench.sym["deck_cue_note"], clip, 36)
            noted = True
        now[0] += 256
    ks = []
    for u in range(14, 20):
        w = out[u * unit_out + 1500: (u + 1) * unit_out - 1000]
        r = math.sqrt(sum(x * x for x in w) / max(1, len(w)))
        ks.append(round((r * math.sqrt(2) - 0.05) / 0.02))
    res.check(ks == [14, 15, 24, 25, 26, 27], f"Bungee takes a cue note 5 ms late at that bar: units {ks} (want [14, 15, 24, 25, 26, 27])")
    b.unstub(0x0804436C)


def t_lineup_bungee_sync(bench, res):
    """3.1.T: Bungee at 90 BPM on a 130 BPM Clip (0.69x) under Clip Deck, transport running: a click
    at every unit lands on the clock from the launch on (Bungee starts where the clock will be at
    the handoff, not ~17 ms ahead), and through the file's own wrap (last unit -> first is straight
    on, no relaunch)."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or "str_stutter" not in bench.sym or "deck_init" not in bench.sym:
        res.check(True, "3.1.T + Clip Deck emu build only (skipped)")
        return
    import math
    b = bench.b
    b.stub(0x0804436C, lambda bb: 0xC3000000)
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    b.call(bench.sym["deck_init"])
    if rb_state(bench) != 1:
        res.check(False, f"bank 2 running ({rb_label(bench)!r})")
        return
    bpm, spu, units = 90.0, 48000 * 60 / 130.0 / 4, 16
    n_src = int(spu * units)

    def click(i):
        f = i - int(int(i // spu) * spu)
        return 0.6 * math.exp(-f / 80) * math.sin(2 * math.pi * 1000 * f / 48000) if f < 400 else 0.0

    table = [click(i) for i in range(n_src)]
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    tp = 0x2001EC00
    out = []

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[table[(pos + i) % n_src] for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)
    b.w32(ctx, tp)
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    clip_level(b, clip)
    b.w32(clip + 0x18, 0x0100)
    b.w32(clip + 0x24, sample)
    b.w32(sample + 0x24, 7)
    b.w32(clip + 0x248, 0xFFFFFFFF)
    b.mu.mem_write(clip + 0xBB1, bytes([5]))
    b.mu.mem_write(clip + 0xBB4, struct.pack("<f", bpm / 130.0))
    b.mu.mem_write(clip + 0xBA8, struct.pack("<q", units * 240))
    b.mu.mem_write(clip + 0x1F4, b"\x01\x01")
    b.mu.mem_write(clip + 0x1F8, b"\x02")
    b.w32(clip + 0x208, 240)
    b.w32(clip + 0x220, 240)
    b.w32(clip + 0x230, units * 240)
    b.w32(clip + 0x240, units)
    b.w32(clip + 0x210, units)
    b.mu.mem_write(clip + 0x1FC, struct.pack("<f", float(spu)))
    for i, v in enumerate((0, POS_END, 0, POS_END)):          # the whole file loops
        b.call(bench.sym["deck_store_unpacked"], 0x0100, ID_DECK + i, v)
    lane = clip + 0x260
    b.mu.mem_write(lane, b"\x00" * 0x260)
    lane_level(b, lane)
    b.mu.mem_write(lane, b"\x01")
    b.w32(lane + 0xD8, 1)
    b.w32(lane + 0xC8, 0)
    for o in (0xCC, 0xD0, 0xD4):
        b.w32(lane + o, 0xFFFFFFFF)
    b.w32(clip + 0x64, b.call(bench.sym["deck_note_unit"], clip, 0))
    b.mu.mem_write(lane + 0xE0, struct.pack("<q", 0))
    b.mu.mem_write(lane + 0x25C, b"\x03")
    b.w32(RB_VOICE + 4, clip)
    b.w32(RB_VOICE + 20, 1)
    per = 3000 / bpm * 240                                     # 8000 output frames per unit
    now = 0
    for _ in range(int((units + 6) * per / 256)):              # past the wrap at unit 16
        b.mu.mem_write(tp, struct.pack("<qqqf", now, 0, 0, bpm))
        b.mu.mem_write(tp + 0x38, b"\x01")
        b.call(bench.sym["rb_mix"], clip, lane, mixbuf, ctx, limit=400_000_000)
        b.call(bench.sym["rb_work"], limit=400_000_000)
        now += 256
    offs = {}
    for i in range(1, units + 5):
        t0, t1 = int(i * per - per / 2), int(i * per + per / 2)
        e = [out[t] ** 2 for t in range(t0, t1)]
        if sum(e) > 1e-6:
            offs[i] = (t0 + sum(k * x for k, x in enumerate(e)) / sum(e) - (i * per + 40)) / 48.0
    early = [offs.get(i) for i in range(1, 7)]
    wrap = [offs.get(i) for i in range(units - 1, units + 5)]
    res.check(all(o is not None and abs(o) < 4.0 for o in early),
              f"Bungee at 0.69x is on the clock from the launch: units 1-6 at {[None if o is None else round(o, 1) for o in early]} ms")
    res.check(all(o is not None and abs(o) < 4.0 for o in wrap),
              f"... and through the file's wrap: units {units - 1}-{units + 4} at {[None if o is None else round(o, 1) for o in wrap]} ms")
    b.unstub(0x0804436C)


def t_lineup_list(bench, res):
    """3.1.T: Warp lists Stutter / Fill / Repitch / Chop / Grain / Bungee (the leaf's RAM label last)."""
    b = bench.b
    words = [b.u32(bench.sym["labels_repitch"] + 4 * i) for i in range(6)]
    names = [bytes(b.mu.mem_read(w, 8)).split(b"\0")[0].decode() for w in words[:5]]
    res.check(names == ["Stutter", "Fill", "Repitch", "Chop", "Grain"], f"names {names}")
    res.check(words[5] == RB_VOICE + 32, f"Bungee's label is the voice's RAM label ({words[5]:#x})")
    if b.u32(REG_PTR) == 0:
        b.call(PARAM_REGISTER_ALL, 0)
    res.check(b.call(0x0808E2A5, 0x19C) == 6 and leaf_v(bench) == 5, "six entries, Bungee at 5")


def t_lineup_rate(bench, res):
    """3.1.T hook_rate: every Warp gets the tempo ratio (clip+0xBB4), Stutter too (it renders from it
    on slices), also when the Clip's flag comes from the per-pad table."""
    b = bench.b
    clip, tempo = 0x2001C800, 0x2001CC00
    b.stub(bench.sym["go_stock_rate"], lambda bb: 0)
    b.call(bench.sym["state_init"])
    got = {}
    for v in range(6):
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        b.mu.mem_write(tempo, b"\x00" * 0x20)
        b.w32(clip + 0x18, 0x100)
        b.mu.mem_write(clip + 0x258, struct.pack("<f", 120.0))
        b.mu.mem_write(tempo + 0x18, struct.pack("<f", 96.0))
        b.mu.mem_write(clip + 0xBB1, bytes([v]))
        b.call(bench.sym["hook_rate"], clip, tempo)
        got[v] = round(struct.unpack("<f", bytes(b.mu.mem_read(clip + 0xBB4, 4)))[0], 4)
    res.check(got == {0: 0.8, 1: 0.8, 2: 0.8, 3: 0.8, 4: 0.8, 5: 0.8}, f"ratio per Warp {got}")
    b.mu.mem_write(clip + 0xBB1, b"\x00")
    b.call(bench.sym["flag_write"], 4, 4)          # the pad table says Grain (key of pad 0x100)
    b.call(bench.sym["hook_rate"], clip, tempo)
    ratio = struct.unpack("<f", bytes(b.mu.mem_read(clip + 0xBB4, 4)))[0]
    res.check(b.u8(clip + 0xBB1) == 4 and abs(ratio - 0.8) < 1e-6, f"flag from the pad table: {b.u8(clip + 0xBB1)}, {ratio:.3f}")
    b.unstub(bench.sym["go_stock_rate"])


def t_lineup_lanes(bench, res):
    """3.1.T: Fill, Chop and Grain render a Clip's lane in bank 2 (rb_lanes): segments on the Sync
    grid locked to the clock, Fill's tail loop and Chop's gaps at a slower tempo, the pad's Level,
    loop off, and Stutter left to stock."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or "str_stutter" not in bench.sym:
        res.check(True, "3.1.T emu build only (skipped)")
        return
    import math
    b = bench.b
    pool, asked = 0xC3000000, []
    b.stub(0x0804436C, lambda bb: (asked.append(bb.arg(0)), pool)[1])
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    res.check(b.u32(RB_VOICE + 76) == pool and asked and b.u32(RB_VOICE + 80) == asked[0],
              f"rb_pair takes the leaf's pool from SDRAM ({[hex(x) for x in asked]})")
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"bank 2 running ({rb_label(bench)!r})")
        return
    seg, n_src = 6000, 192000                       # 1/16 at 120 BPM, 8 beats
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    tp = 0x2001EC00
    lens = {sample: n_src}
    out = []

    def src(i):
        k = (i % n_src) // seg
        return (0.2 + 0.1 * (k % 3)) * math.sin(2 * math.pi * 440 * i / 48000)

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[src(pos + i) for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: lens[bb.arg(0)]), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)
    now = [0]
    b.w32(ctx, tp)

    def setup(mode, loop=1, level=1.0, beats=8):
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        clip_level(b, clip, level)
        b.w32(clip + 0x18, 0x107)
        b.w32(clip + 0x24, sample)
        b.w32(sample + 0x24, 7)
        b.w32(clip + 0x248, 0xFFFFFFFF)
        b.mu.mem_write(clip + 0xBB1, bytes([mode]))
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
        b.mu.mem_write(clip + 0xBA8, struct.pack("<q", int(beats * 960)))
        b.mu.mem_write(clip + 0x1F4, bytes([loop]))
        b.mu.mem_write(clip + 0x1F5, b"\x01")                     # Sync 1/16
        b.mu.mem_write(clip + 0x220, struct.pack("<q", 240))
        b.w32(clip + 0x210, n_src // seg)
        b.mu.mem_write(clip + 0x1FC, struct.pack("<f", float(seg)))
        lane = clip + 0x260
        b.mu.mem_write(lane, b"\x00" * 0x260)
        lane_level(b, lane, level)
        b.mu.mem_write(lane, b"\x01")
        b.w32(lane + 0xD8, 1)
        b.w32(lane + 0xC8, 0)
        b.mu.mem_write(lane + 0xE0, struct.pack("<q", now[0] + 1))
        b.mu.mem_write(lane + 0x25C, b"\x03")
        return lane

    def run(lane, blocks):
        out.clear()
        rets = set()
        for _ in range(blocks):
            b.mu.mem_write(tp, struct.pack("<qqqf", now[0], 0, 0, 96.0))   # 96 BPM: 0.8 x the Clip
            b.mu.mem_write(tp + 0x38, b"\x01")
            rets.add(b.call(bench.sym["rb_lanes"], clip, lane, mixbuf, ctx, limit=400_000_000))
            now[0] += 256
        return rets

    def rms(xs):
        return math.sqrt(sum(x * x for x in xs) / max(1, len(xs)))

    def windows():
        return [rms(out[i:i + 256]) for i in range(0, len(out) - 255, 256)]

    lane = setup(1)
    rets = run(lane, 120)
    w = windows()
    res.check(rets == {1}, f"Fill renders the lane ({rets})")
    res.check(len(w) == 120 and min(w[1:]) > 0.05, f"Fill at 0.8x: no gaps (quietest block {min(w[1:]):.3f})")
    fill_level = rms(out)

    lane = setup(3)
    run(lane, 120)
    w = windows()
    quiet = [i for i, x in enumerate(w) if x < 0.01]
    res.check(len(quiet) >= 8, f"Chop at 0.8x: gaps at the segment ends ({len(quiet)} silent blocks)")
    def onsets_of(w):
        on = []
        for i in range(2, len(w)):
            if w[i] > 0.05 and min(w[i - 2:i]) < 0.01 and (not on or i * 256 - on[-1] > 1024):
                on.append(i * 256)
        return on

    onsets = onsets_of(w)
    off = [((o + 128) % 7500) for o in onsets]
    res.check(len(onsets) >= 3 and all(min(x, 7500 - x) < 600 for x in off),
              f"Chop: segments start on the clock's grid (every 7500 frames; {onsets})")

    # Sync = Slice: the segments are the slice markers (0, 9000, 12000, 21000).
    table = 0x2001D800
    lane = setup(3)
    b.mu.mem_write(table, struct.pack("<4I", 0, 9000, 12000, 21000))
    b.mu.mem_write(clip + 0x220, struct.pack("<q", 0xF0000))
    b.w32(clip + 0x24C, table)
    b.w32(clip + 0x250, 4)
    run(lane, 120)
    onsets = onsets_of(windows())
    want = [9000 / 0.8, 12000 / 0.8, 21000 / 0.8]
    res.check(len(onsets) >= 3 and all(abs(o - x) < 600 for o, x in zip(onsets, want)),
              f"Sync = Slice: segments start at the slices ({onsets}, want {[round(x) for x in want]})")

    lane = setup(4)
    rets = run(lane, 120)
    w = windows()
    res.check(rets == {1} and sum(1 for x in w if x > 0.02) >= 100, f"Grain renders ({sum(1 for x in w if x > 0.02)} of {len(w)} blocks)")

    lane = setup(1, level=1.0 - 6.0 / 64)            # Level -6 dB
    run(lane, 120)
    ratio = rms(out) / fill_level
    res.check(abs(ratio - 10 ** (-6 / 20)) < 0.03, f"the pad's Level applies: -6 dB -> x{ratio:.3f}")

    lens[sample] = 20000
    lane = setup(1, loop=0, beats=8 * 20000 / n_src)
    run(lane, 120)
    res.check(b.u8(lane) == 0, "loop off: the lane ends at the end of the file")
    lens[sample] = n_src

    lane = setup(0)
    res.check(run(lane, 2) == {0}, "Stutter: stock grains (rb_lanes declines)")
    b.unstub(0x0804436C)


def t_lineup_lanes_timing(bench, res):
    """3.1.T, the unit's reports: Grain starts after a quantised launch's long pre-delay and lands on
    the clock; Fill keeps playing through a jittery clock (no segment restarts) at a slow tempo, with
    its tail loop search spread over the segment (no block-sized CPU spike); Stutter on slices runs on
    like stock Stretch, with the segments at the slices."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path or "str_stutter" not in bench.sym:
        res.check(True, "3.1.T emu build only (skipped)")
        return
    import math
    from unicorn import UC_HOOK_BLOCK
    b = bench.b
    b.stub(0x0804436C, lambda bb: 0xC3000000)
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"bank 2 running ({rb_label(bench)!r})")
        return
    spu = 48000 * 60 / 130.0 / 4                    # 1/16 of a 130 BPM Clip
    units = 32
    n_src = int(spu * units)
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001E400, 0x2001E800)
    tp = 0x2001EC00
    out = []
    shape = {"f": None}

    def click(i):                                   # a short 1 kHz click at each unit's start
        f = i - int(int(i // spu) * spu)
        return 0.6 * math.exp(-f / 80) * math.sin(2 * math.pi * 1000 * f / 48000) if f < 400 else 0.0

    def ramp(i):                                    # each unit rises 0.1 -> 0.9
        k = int(i // spu)
        return (0.1 + 0.8 * (i - k * spu) / spu) * math.sin(2 * math.pi * 440 * i / 48000)

    tables = {}

    def pcm_read(bb):
        dest, count, pos = bb.arg(1), bb.arg(2), bb.arg(3)
        t = tables[shape["f"]]
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[t[(pos + i) % n_src] for i in range(count)]))
        return count

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    def buf_clear(bb):
        bb.mu.mem_write(planes[0], b"\0" * 1024)
        bb.mu.mem_write(planes[1], b"\0" * 1024)

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: out.extend(struct.unpack("<256f", bytes(bb.mu.mem_read(planes[0], 1024))))),
                     *LANE_STUBS):
        b.stub(addr, fn)
    b.w32(ctx, tp)
    now = [0]

    def setup(mode, wave, bpm, predelay=0, sync=1, slices=None):
        shape["f"] = wave
        if wave not in tables:
            tables[wave] = [wave(i) for i in range(n_src)]
        b.mu.mem_write(clip, b"\x00" * 0xC00)
        clip_level(b, clip)
        b.w32(clip + 0x18, 0x107)
        b.w32(clip + 0x24, sample)
        b.w32(sample + 0x24, 7)
        b.w32(clip + 0x248, 0xFFFFFFFF)
        b.mu.mem_write(clip + 0xBB1, bytes([mode]))
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", bpm / 130.0))
        b.mu.mem_write(clip + 0xBA8, struct.pack("<q", units * 240))
        b.mu.mem_write(clip + 0x1F4, b"\x01")
        b.mu.mem_write(clip + 0x1F5, bytes([sync]))
        b.mu.mem_write(clip + 0x220, struct.pack("<q", 240))
        b.w32(clip + 0x210, units)
        b.mu.mem_write(clip + 0x1FC, struct.pack("<f", float(spu)))
        if slices:
            table = 0x2001D800
            b.mu.mem_write(table, struct.pack(f"<{len(slices)}I", *slices))
            b.mu.mem_write(clip + 0x220, struct.pack("<q", 0xF0000))
            b.w32(clip + 0x24C, table)
            b.w32(clip + 0x250, len(slices))
        lane = clip + 0x260
        b.mu.mem_write(lane, b"\x00" * 0x260)
        lane_level(b, lane)
        b.mu.mem_write(lane, b"\x01")
        b.w32(lane + 0xD8, 1)
        b.w32(lane + 0xC8, 0)
        b.mu.mem_write(lane + 0xE0, struct.pack("<q", now[0]))
        b.w32(lane + 0x138, predelay)
        b.mu.mem_write(lane + 0x25C, b"\x03")
        return lane

    cost = [0]
    hook = [None]

    def run(lane, blocks, bpm, jitter=0, count=False):
        out.clear()
        costs, rets = [], set()
        rng = 12345
        if count:
            hook[0] = b.mu.hook_add(UC_HOOK_BLOCK, lambda mu, a, sz, d: cost.__setitem__(0, cost[0] + sz // 2),
                                    begin=0x08000000, end=0x081FFFFF)
        for _ in range(blocks):
            rng = (rng * 1103515245 + 12345) & 0x7FFFFFFF
            j = (rng >> 16) % (2 * jitter + 1) - jitter if jitter else 0
            b.mu.mem_write(tp, struct.pack("<qqqf", now[0] + j, 0, 0, bpm))
            b.mu.mem_write(tp + 0x38, b"\x01")
            cost[0] = 0
            rets.add(b.call(bench.sym["rb_lanes"], clip, lane, mixbuf, ctx, limit=400_000_000))
            costs.append(cost[0])
            now[0] += 256
        if count:
            b.mu.hook_del(hook[0])
        return rets, costs

    def rms(xs):
        return math.sqrt(sum(x * x for x in xs) / max(1, len(xs)))

    # Grain, launched with a 600-frame quantise delay (more than a block): the grains start.
    lane = setup(4, ramp, 90.0, predelay=600)
    rets, _ = run(lane, 40, 90.0)
    loud = sum(1 for i in range(0, len(out) - 255, 256) if rms(out[i:i + 256]) > 0.02)
    res.check(rets == {1} and rms(out[:512]) == 0.0 and loud >= 30,
              f"Grain after a 600-frame pre-delay: silent first, then plays ({loud} of 40 blocks)")

    # Grain at 90 BPM: each click's energy centre against where the clock has it.
    def centres(bpm, first=2):
        per = 3000 / bpm * 240
        offs = []
        i = first
        while (i + 1) * per < len(out):
            t0, t1 = int(i * per - per / 2), int(i * per + per / 2)
            e = [out[t] ** 2 for t in range(t0, t1)]
            if sum(e) > 1e-6:
                offs.append((t0 + sum(k * x for k, x in enumerate(e)) / sum(e) - (i * per + 40)) / 48.0)
            i += 1
        return offs

    lane = setup(4, click, 90.0)
    run(lane, int(4 * 48000 / 256), 90.0)
    offs = centres(90.0)
    mean = sum(offs) / max(1, len(offs))
    res.check(len(offs) >= 20 and abs(mean) < 3.0,
              f"Grain at 0.69x lands on the clock: click centres {mean:+.1f} ms on average over {len(offs)} units "
              f"(Window 100 spreads them {min(offs):+.0f}..{max(offs):+.0f} ms)")

    # Fill at 70 BPM with the clock jittering +-3 frames: every unit still plays its rise from the start
    # (a restart each time the clock steps back would hold it at the start's level, a buzz).
    lane = setup(1, ramp, 70.0)
    blocks = int(12 * spu / (70 / 130) / 256)
    _, costs = run(lane, blocks, 70.0, jitter=3, count=True)
    per = spu / (70 / 130)
    rises = []
    for u in range(1, 11):
        s0 = int(u * per)
        a, z = rms(out[s0 + 300:s0 + 556]), rms(out[s0 + int(spu) - 600:s0 + int(spu) - 344])
        rises.append(round(z / max(a, 1e-6), 1))
    res.check(all(r > 3 for r in rises), f"Fill at 0.54x, clock jitter +-3 frames: each unit rises (end / start {rises})")
    costs = sorted(costs)
    res.check(costs[-1] < 200_000, f"Fill's loop search is spread: max {costs[-1]} instructions per block "
                                   f"(median {costs[len(costs) // 2]}; one search at the tail was ~490k)")

    # Stutter with Sync = Slice: renders here, runs on at 0.69x (no gaps), and starts at the slices.
    sl = [0, int(3 * spu), int(5 * spu), int(9 * spu)]
    lane = setup(0, click, 90.0, slices=sl)
    rets, _ = run(lane, int(14 * spu / (90 / 130) / 256), 90.0)
    want = [x / (90 / 130) for x in sl[1:]]
    ons = []
    for x in want:
        w = out[int(x) - 400:int(x) + 1200]
        k = next((i for i, v in enumerate(w) if abs(v) > 0.2), None)
        ons.append(None if k is None else int(x) - 400 + k)
    res.check(rets == {1} and all(o is not None and abs(o - x) < 300 for o, x in zip(ons, want)),
              f"Stutter, Sync = Slice: segments at the slices ({ons}, want {[round(x) for x in want]})")
    # Run-on: segment 0 (source units 0-2) plays on at 1x past its end, into slice 1's audio: unit 3's
    # click sounds at output frame 3 x spu, before slice 1 itself is due (3 x spu / 0.69). Chop would
    # gate there.
    t = int(3 * spu)
    w = out[t - 100:t + 600]
    res.check(max(abs(v) for v in w) > 0.2, "Stutter on slices runs on into the next slice's audio, as stock")
    lane = setup(0, click, 90.0)
    rets, _ = run(lane, 2, 90.0)
    res.check(rets == {0}, "Stutter on a grid: stock grains (rb_lanes declines)")
    b.unstub(0x0804436C)


def t_warp_list_per_pad(bench, res):
    """3.1.B: the leaf is offered only on a pad that has it or in its pad's Excl Group (the Warp
    row's count, b2pair.c warp_count at the pad page's bl 0x0808E2A4)."""
    if "warp_count" not in bench.sym:
        res.check(True, "not in this image (skipped)")
        return
    b = bench.b
    if b.u32(REG_PTR) == 0:
        b.call(PARAM_REGISTER_ALL, 0)
    full = b.call(0x0808E2A5, 0x19C)
    leaf = leaf_v(bench)
    res.check(full == leaf + 1, f"Warp list has {full} entries, the leaf last ({leaf})")
    params = {}

    def bag(row, col):
        return 0x24020088 + 0x1840 + row * 0xF0 + col * 0x30

    b.stub(0x08093E9C, lambda bb: params.get((bb.arg(0), bb.arg(1)), 0))
    root = 0x2406F000 - (0x732D8 + 0xD814)            # so the pad key lands in mapped RAM
    b.w32(0x24002E6C, root)

    def count_for(row, col):
        b.mu.mem_write(root + 0x732D8 + 0xD814, struct.pack("<H", row << 4 | col))
        return b.call(bench.sym["warp_count"], 0x19C)

    def pad(row, col, warp, group, mode=1):
        params[(bag(row, col), 0x19C)] = warp
        params[(bag(row, col), 0xC4)] = group
        params[(bag(row, col), 0x8B)] = mode

    res.check(count_for(0, 0) == full and b.call(bench.sym["warp_count"], 0x8B) == b.call(0x0808E2A5, 0x8B),
              f"no leaf anywhere: full list ({full}); other params untouched")
    pad(0, 0, leaf, 1)                               # pad A1: leaf, Excl A
    pad(0, 1, 0, 1)                                  # same group
    pad(0, 2, 0, 2)                                  # other group
    pad(0, 3, 0, 0)                                  # no group
    res.check(count_for(0, 0) == full, "the leaf's own pad keeps it")
    res.check(count_for(0, 1) == full, "same Excl Group: offered")
    res.check(count_for(0, 2) == full - 1, "other Excl Group: not offered")
    res.check(count_for(0, 3) == full - 1, "no Excl Group: not offered")
    pad(0, 0, leaf, 0)                               # the leaf's pad in no group
    res.check(count_for(0, 1) == full - 1, "leaf pad without a group: nowhere else")
    pad(0, 0, leaf, 1)
    pad(0, 0 + 4, 0, 1, mode=2)
    res.check(count_for(0, 4) == full, "count is per pad, not per mode (the Slicer row is not changed)")


def t_sd_read_retry(bench, res):
    """A failed sample block read is retried after clearing FatFs's sticky error and seeking back
    (stock zero-fills it, marks it valid, and every later block of the sample then fails too)."""
    if "sd_read_retry" not in bench.sym:
        res.check(True, "not in this image (skipped)")
        return
    b = bench.b
    wave, fil, dest = 0x2001F000, 0x2001F100, 0x2001F200
    b.mu.mem_write(wave, b"\x00" * 0x40)
    b.mu.mem_write(fil, b"\x00" * 0x40)
    b.w32(wave + 4, fil)
    b.mu.mem_write(wave + 0x28, b"\x01")
    b.w32(wave + 0xC, 0x123400)
    b.w32(0x38007100, 0x31474144)                    # diag running
    b.w32(0x38007100 + 40, 0)
    fails, seeks = [0], []

    def read(bb):
        if fails[0] > 0:
            fails[0] -= 1
            bb.mu.mem_write(fil + 0x31, b"\x01")    # f_read failed: the error sticks
            return 0
        return 1 if bb.u8(fil + 0x31) == 0 else 0

    b.stub(0x0807714C, read)
    b.stub(0x08086D96, lambda bb: seeks.append((bb.arg(0), bb.arg(2))) or 0)
    fails[0] = 0
    res.check(b.call(bench.sym["sd_read_retry"], wave, dest, 0xC000) == 1 and not seeks, "a good read: no retry")
    fails[0] = 2
    ok = b.call(bench.sym["sd_read_retry"], wave, dest, 0xC000)
    res.check(ok == 1 and seeks == [(fil, 0x123400), (fil, 0x123400)] and b.u8(fil + 0x31) == 0,
              f"two failures: error cleared, seek back, third try reads ({ok}, {seeks})")
    seeks.clear()
    fails[0] = 9
    ok = b.call(bench.sym["sd_read_retry"], wave, dest, 0xC000)
    res.check(ok == 0 and len(seeks) == 3, f"a card that keeps failing: three retries, then failure ({len(seeks)})")
    res.check(b.u16(0x38007100 + 40) == 2 and b.u16(0x38007100 + 42) == 1, "counted: 2 retried, 1 still failed")


def t_rubber_broken_left_alone(bench, res):
    """A payload whose start crashed is not rewritten: that turned one crash into one per preset load."""
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    b.mu.mem_write(RB_B2 + 0xC0000, struct.pack("<II", 0x43424252, ~0x43424252 & 0xFFFFFFFF))
    rb_bkp(bench, booting=0x544F4252)
    b.call(bench.sym["rb_pair"])
    res.check(rb_state(bench) == 4, f"broken ({rb_state(bench)})")
    b.mu.mem_write(0x240021A4 + 0x15, b"\x01")
    FakeFile(b, rb_file(bench), pay)
    fm = FlashModel(b)
    b.watch(bench.sym["rubber_boot"])
    rb_poll(bench)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased and not fm.words and not fm.reset, "loader leaves it: no erase, no write, no restart")
    res.check(b.u32(RB_BKP + 16) == 0x544F4252, "boot mark kept")
    res.check(b.calls[bench.sym["rubber_boot"] & ~1] == 0, "not started")


def t_rubber_stage_label(bench, res):
    """A boot that died mid-stage: the next boot names the stage after the label."""
    b = bench.b
    rb_bkp(bench)
    b.w32(RB_BKP + 20, 0x32)                    # died erasing sector 2
    b.call(bench.sym["rb_pair"])
    res.check(rb_label(bench) == rb_name(bench) + "! 32", f"label {rb_label(bench)!r}")
    res.check(b.u32(RB_BKP + 20) == 0, "stage cleared")
    b.call(bench.sym["rb_pair"])
    res.check(rb_label(bench) == rb_name(bench) + "!", f"next clean boot: {rb_label(bench)!r}")


def diag_tools_label(bench):
    """Run cpu_mid with a stub label setter; return the middle label's text."""
    b = bench.b
    got = {}
    b.stub(0x080A3DEC, lambda bb: got.__setitem__(bb.arg(0), bb.cstr(bb.arg(1))))
    mid = 0x2001E100
    b.call(bench.sym["cpu_mid"], mid)
    b.unstub(0x080A3DEC)
    return got.get(mid), "12.3 GB"


def t_diag_cpu_meter(bench, res):
    b = bench.b
    clock = [0]
    b.w32(0xE0001004, 0)
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    period, busy = 2_560_000, 1_000_000

    def receive(bb):
        clock[0] += period - busy               # idle until the next block
        bb.w32(0xE0001004, clock[0])
        return 1

    b.stub(0x08088756, receive)
    for _ in range(400):
        b.call(bench.sym["cpu_wait"], 0, 0, 0, 0)
        clock[0] += busy                        # the render
        b.w32(0xE0001004, clock[0])
    mid, space = diag_tools_label(bench)
    res.check(space == "12.3 GB", f"free space still set ({space!r})")
    res.check(mid == "39/39%", f"load {mid!r} (1.0M of 2.56M cycles = 39%)")
    b.w32(RB_VOICE + 8, 1)                      # Rubber running, 120 % of a block on its own
    b.w32(RB_VOICE + 48, 25600 * 120)          # rb_mix's smoothed cost (RbVoice.cyc_avg)
    b.w32(RB_VOICE + 56, 25600 * 131)          # and its worst block (cyc_peak)
    for _ in range(200):
        b.stub(0x08088756, receive)
        b.call(bench.sym["cpu_wait"], 0, 0, 0, 0)
        clock[0] += busy
        b.w32(0xE0001004, clock[0])
    mid, _ = diag_tools_label(bench)
    res.check(mid == "39/39% R120/131", f"with Rubber's own share: {mid!r}")
    res.check(b.u32(RB_VOICE + 56) == 0, "peak starts again after the look")
    res.check(b.u32(0x50003004) & (1 << 9), "watchdog early wakeup armed")


def t_diag_fault_record(bench, res):
    """A fault: the handler records PC/LR/CFSR and the stage, resets; the next boot shows it on Tools."""
    b = bench.b
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.w32(RB_BKP + 20, 0x11)                    # in rubber_boot
    frame = 0x2001E000
    b.mu.mem_write(frame, struct.pack("<8I", 1, 2, 3, 4, 12, 0x08123457, 0x08123456, 0x01000000))
    b.w32(0xE000ED28, 0x00008200)               # CFSR: precise bus fault, BFAR valid
    b.w32(0xE000ED38, 0x0C000000)
    flushed = []
    from unicorn import UC_HOOK_MEM_WRITE
    h = b.mu.hook_add(UC_HOOK_MEM_WRITE, lambda mu, acc, a, sz, v, d: flushed.append(v), begin=0xE000EF68, end=0xE000EF6B)
    stopped = [False]

    def on_aircr(mu, access, addr, size, value, data):
        if value & 4:
            stopped[0] = True
            mu.emu_stop()

    h2 = b.mu.hook_add(UC_HOOK_MEM_WRITE, on_aircr, begin=0xE000ED0C, end=0xE000ED0F)
    try:
        b.call(bench.sym["fault_record"], frame, 0xFFFFFFFD)
    except RuntimeError:
        pass
    b.mu.hook_del(h)
    b.mu.hook_del(h2)
    r = [b.u32(RB_BKP + 0x40 + 4 * i) for i in range(12)]
    res.check(r[0] == 0x31544C46 and r[1] == 0x08123456 and r[2] == 0x08123457, f"PC/LR recorded {[hex(x) for x in r[:3]]}")
    res.check(r[4] == 0x8200 and r[7] == 0x0C000000 and r[11] == 0x11, f"CFSR, BFAR, stage {hex(r[4])} {hex(r[7])} {hex(r[11])}")
    res.check(stopped[0], "reset requested")
    res.check(0x38800040 in flushed and 0x38800060 in flushed, "record cleaned out of the data cache before the reset")
    b.call(bench.sym["rb_pair"])                # the next boot
    mid, _ = diag_tools_label(bench)
    res.check(mid == "F08123456", f"Tools shows the fault PC: {mid!r}")
    res.check(rb_label(bench) == rb_name(bench) + "! 11", f"Rubber label names the stage: {rb_label(bench)!r}")
    b.call(bench.sym["rb_pair"])                # and the one after
    mid, _ = diag_tools_label(bench)
    res.check(mid != "F08123456", f"gone one boot later: {mid!r}")


def t_diag_vectors(bench, res):
    img = bench.b.image
    h = bench.sym["fault_handler"] | 1
    vec = [struct.unpack_from("<I", img, off)[0] for off in (0x0C, 0x10, 0x14, 0x18, 0x40)]
    res.check(vec[:4] == [h] * 4, f"fault vectors -> fault_handler {[hex(v) for v in vec[:4]]}")
    res.check(vec[4] == bench.sym["wdg_handler"] | 1, f"IRQ 0 -> wdg_handler {vec[4]:#x}")



def t_diag_tools_gui(bench, res):
    """tools_thunk (the Tools refresh's free-space query) fills the screen's middle label, then
    runs the stock query with its arguments intact."""
    b = bench.b
    sets, query = [], []
    b.stub(0x080A3DEC, lambda bb: sets.append((bb.arg(0), bb.cstr(bb.arg(1), 16))))
    b.stub(0x0809BBF8, lambda bb: query.append((bb.arg(0), bb.arg(1))) or 0)
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    screen = 0x2001D800
    b.mu.reg_write(bd.A.UC_ARM_REG_R7, screen)
    b.call(bench.sym["tools_thunk"], 0x1234, 0x5678)
    res.check([a for a, t in sets] == [screen + 0xD4], f"middle label set ({[(hex(a), t) for a, t in sets]})")
    res.check(query == [(0x1234, 0x5678)], f"stock query runs with its args {query}")
    img = b.image
    off = 0x080C5264 - 0x08040000
    res.check(img[off:off + 4] != bytes.fromhex("d6f7c8fc"), "0x080C5264 patched")



def t_rubber_takeover(bench, res):
    """Bank 2 holds blackbox-mod's payload: left alone, unless RBTAKE.TXT is on the card."""
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    other = bytes([0x42, 0x42, 0x32, 0x21]) + bytes(range(256)) * 600
    b.mu.mem_write(RB_B2, other)
    b.mu.mem_write(RB_B2 + 0xC0000, struct.pack("<II", 0x43324242, ~0x43324242 & 0xFFFFFFFF))   # "BB2C"
    calib = bytes(range(256)) * 16
    b.mu.mem_write(0x081E0000, calib)
    rb_bkp(bench)
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), pay)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(rb_label(bench) == rb_name(bench) + "! x" and not fm.erased and not fm.words, f"without RBTAKE.TXT: left alone, {rb_label(bench)!r}")
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), pay, others={"RBTAKE.TXT"})
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(fm.reset, "with RBTAKE.TXT: written, restart")
    res.check(bytes(b.mu.mem_read(RB_B2, len(pay))) == bytes(pay), "payload in bank 2")
    res.check(b.u32(RB_B2 + 0xC0000) == 0x43424252, "our claim replaced theirs")
    res.check(sorted(set(fm.erased)) == list(range(7)), f"sectors 0-6 erased {sorted(set(fm.erased))}")
    res.check(bytes(b.mu.mem_read(0x081E0000, len(calib))) == calib and not fm.bad, f"sector 7 untouched {fm.bad[:2]}")
    b.call(bench.sym["rb_pair"])
    res.check(rb_state(bench) == 3, f"next boot pairs ({rb_state(bench)})")


def t_rubber_release(bench, res):
    """RBFREE.TXT: bank 2 erased (ours only), Rubber off, nothing written though BBRUBBER.BIN is there."""
    pay = rb_payload(bench)
    if pay is None:
        res.check(False, "BBRUBBER payload beside the image")
        return
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    b.mu.mem_write(RB_B2 + 0xC0000, struct.pack("<II", 0x43424252, ~0x43424252 & 0xFFFFFFFF))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.w32(RB_VOICE + 8, 1)                      # running
    b.mu.mem_write(0x240021A4 + 0x15, b"\x01")
    FakeFile(b, rb_file(bench), pay, others={"RBFREE.TXT"})
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(rb_state(bench) == 2 and rb_label(bench).endswith(" f"), f"Rubber off, {rb_label(bench)!r}")
    res.check(bytes(b.mu.mem_read(RB_B2, 0xE0000)) == b"\xff" * 0xE0000, "sectors 0-6 blank (blackbox-mod's loader takes it)")
    res.check(not fm.words and not fm.reset and not fm.bad, "nothing written, no restart")
    rb_loader_boot(bench)
    FakeFile(b, rb_file(bench), pay, others={"RBFREE.TXT"})
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased and not fm.words, "next boot with RBFREE.TXT still there: stays blank")



def t_rubber_cpu_guard(bench, res):
    """A render that keeps the block busy switches Rubber off before the watchdog would; a cheaper
    one does not. The cost stays readable on the Tools bar."""
    b = bench.b
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    fake_mix = 0x08180001
    b.mu.mem_write(RB_B2, bytes(20) + struct.pack("<I", fake_mix) + bytes(8))
    cost = [1_000_000]

    def render(bb):
        bb.w32(0xE0001004, (bb.u32(0xE0001004) + cost[0]) & 0xFFFFFFFF)

    b.stub(fake_mix, render)
    b.w32(RB_VOICE + 8, 1)
    for _ in range(100):
        b.call(bench.sym["rb_mix"], 1, 2, 3, 4)
    res.check(rb_state(bench) == 1, f"39 % per block: keeps running ({rb_state(bench)})")

    def run_until_off(costs, limit):
        for i in range(limit):
            cost[0] = costs[i % len(costs)]
            b.call(bench.sym["rb_mix"], 1, 2, 3, 4)
            if rb_state(bench) != 1:
                return i + 1
        return None

    def fresh():
        b.w32(RB_VOICE + 8, 1)
        b.w32(RB_VOICE + 48, 1_000_000)          # cyc_avg as after the 39 % blocks
        b.w32(RB_VOICE + 52, 0)                  # over_n

    # 90 % every block: off once the smoothed cost has been over 75 % for 32 blocks.
    n = run_until_off([2_300_000], 120)
    res.check(n is not None and 32 < n <= 70, f"90 % blocks: off after {n} blocks")
    res.check(rb_state(bench) == 5 and rb_label(bench) == rb_name(bench) + "! cpu", f"off, {rb_label(bench)!r}")
    # Bungee's pattern: a lighter and a heavier block in turn (one half-grain each). 60 / 95 %
    # averages 77 %: a per-block test never saw 32 in a row over 75 %; the smoothed one does.
    fresh()
    n = run_until_off([1_540_000, 2_430_000], 200)
    res.check(n is not None, f"alternating 60/95 % blocks: off after {n} blocks")
    # The leaf cheap (30 %), the audio task as a whole over budget (diag_load 150 %): off too.
    fresh()
    b.w32(0x38007100, 0x31474144)                # diag magic "DAG1"
    b.w32(0x38007100 + 12, 2_560_000)            # nominal block known
    b.w32(0x38007100 + 36, 1500)                 # load_ema, per mille
    n = run_until_off([770_000], 60)
    res.check(n == 32, f"leaf at 30 %, audio task at 150 %: off after {n} blocks")
    b.w32(0x38007100 + 36, 400)
    b.watch(fake_mix)
    b.call(bench.sym["rb_mix"], 1, 2, 3, 4)
    res.check(b.calls[fake_mix & ~1] == 0, "no render after that")
    b.call(bench.sym["rb_ready"])
    res.check(b.call(bench.sym["rb_ready"]) == 0, "lane hook sees Rubber off (stock grains)")
    b.w32(0xE0001004, 0)
    b.stub(0x08088756, lambda bb: 1)
    for _ in range(3):
        b.call(bench.sym["cpu_wait"], 0, 0, 0, 0)
    mid, _ = diag_tools_label(bench)
    import re
    m = re.search(r"R(\d+)/\d+$", mid or "")
    res.check(m is not None and 25 <= int(m.group(1)) <= 40, f"cost still on Tools (the last, 30 % leaf): {mid!r}")



def t_rubber_cost_flat(bench, res):
    """emu build: one hop per block at every tempo, so the cost per block is flat over time and
    the same at 0.5x, 1x and 2x; the tempo is right (source frames per output frame)."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path:
        res.check(True, "cost run needs the --emu build (skipped)")
        return
    import math
    from unicorn import UC_HOOK_BLOCK
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    if rb_state(bench) != 1:
        res.check(False, f"Rubber running ({rb_label(bench)!r})")
        return
    sr, n_src = 48000, 48000 * 30
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x2001A000, 0x2001B000)
    reads = [0]
    work = [0]
    grains = []      # Bungee: (clip position, frames) of each left-channel read run, one per grain

    def pcm_read(bb):
        dest, count, pos, ch = bb.arg(1), bb.arg(2), bb.arg(3), bb.arg(4)
        if ch == 0:
            reads[0] += count
            if grains and grains[-1][2] and (grains[-1][0] + grains[-1][1]) % n_src == pos:
                grains[-1] = (grains[-1][0], grains[-1][1] + count, True)   # same run, past the wrap
            else:
                grains.append((pos, count, True))
        elif grains:
            grains[-1] = (grains[-1][0], grains[-1][1], False)
        bb.mu.mem_write(dest, struct.pack(f"<{count}f", *[drums(pos + i) for i in range(count)]))
        return count

    def drums(t):
        # a 220 Hz tone plus a noise hit every quarter second: R2's transient path runs
        hit = (t % 12000)
        noise = ((t * 1103515245 + 12345) >> 8 & 0xFFFF) / 32768.0 - 1.0
        return 0.25 * math.sin(2 * math.pi * 220 * t / sr) + 0.6 * math.exp(-hit / 400.0) * noise

    def buf_planes(bb):
        bb.w32(bb.arg(1), planes[0])
        bb.w32(bb.arg(2), planes[1])

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, lambda bb: 0), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, lambda bb: 0),
                     *LANE_STUBS):
        b.stub(addr, fn)
    b.mu.hook_add(UC_HOOK_BLOCK, lambda mu, a, sz, d: work.__setitem__(0, work[0] + sz), begin=0, end=0xFFFF)
    b.mu.hook_add(UC_HOOK_BLOCK, lambda mu, a, sz, d: work.__setitem__(0, work[0] + sz), begin=RB_B2, end=0x081FFFFF)
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    clip_level(b, clip)
    b.w32(clip + 0x24, sample)
    b.w32(ctx, 0)                               # no transport: no clock lock
    b.mu.mem_write(clip + 0x260, b"\x01")
    means = {}
    for ratio in (1.0, 0.5, 2.0, 0.8, 1.25):
        b.mu.mem_write(clip + 0xBB4, struct.pack("<f", ratio))
        b.w32(RB_VOICE + 20, 1)                 # relaunch
        grains.clear()
        per, task = [], []
        for i in range(140):
            work[0] = 0
            r0 = reads[0]
            b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=200_000_000)
            audio = work[0]
            if "rb_work" in bench.sym:              # the leaf task's wake, after the block
                b.call(bench.sym["rb_work"], limit=200_000_000)
            if i == 60:
                start_reads = reads[0]
            if i >= 60:
                # Bungee (3.1.B) renders in its own task: the audio block's cost is rb_mix alone,
                # flat; the task's grains land in whichever wake has ring space.
                per.append(audio if "rb_work" in bench.sym else work[0])
                task.append(work[0])
        rate = (reads[0] - start_reads) / (79 * 256)
        if rb_name(bench) == "Bungee":
            # The window slides (bungee.cpp fill_window): each clip frame is read once, so frames
            # read per output frame is the tempo too (it was ~8 when every grain read its 4096).
            res.check(abs(rate - ratio) < 0.03 * ratio + 0.01, f"{ratio}x: clip frames read per output frame {rate:.3f}")
            # The tempo itself from the window's step through the clip (input hop = ratio x 512).
            starts = [p for p, c, _ in grains[-60:] if c >= 64]
            steps = sorted((b2 - a2) % n_src for a2, b2 in zip(starts, starts[1:]))
            rate = steps[len(steps) // 2] / 512 if steps else 0.0
        mean = sum(per) / len(per)
        means[ratio] = sum(task) / len(task)
        res.check(max(per) <= 1.35 * mean, f"{ratio}x: flat, peak/mean {max(per) / mean:.2f}")
        res.check(abs(rate - ratio) < 0.03 * ratio + 0.01, f"{ratio}x: tempo {rate:.3f}")
    off_unity = [v for k, v in means.items() if k != 1.0]   # exactly 1.0x is cheaper (R2's unity path)
    spread = max(off_unity) / min(off_unity)
    unity_ok = means[1.0] <= max(off_unity)
    if rb_name(bench) == "Bungee":                          # no unity shortcut: 1.0x is just one more tempo
        spread = max(means.values()) / min(means.values())
        unity_ok = True
    res.check(spread <= 1.2 and unity_ok, f"same cost at every tempo (max/min {spread:.2f}, " +
              ", ".join(f"{k}x {v / 1e6:.2f}M" for k, v in means.items()) + ")")


# ---- Clip regions (3.1.E): Start / End / Loop Start / Loop End in beats, ids 0x19D..0x1A0
ID_DECK = 0x19D
DK_BASE = 0x38004930     # DkState (deck.c): free on this line and the Bungee / Mix line
CLIP_STARTGRAIN = 0x08065550


def t_deck_pos_ids(bench, res):
    img = bench.b.image
    clip = _u16s(img, CLIP_POS, 20)
    want = [0x8D, 0xD2, 0x47, 0x94, ID_REPITCH, ID_CUEMODE, ID_CUENOTE, ID_GRID] + list(range(ID_CUE1, ID_CUE1 + 8))
    want += [0] * (20 - len(want))
    res.check(clip == want, f"Clip Pos ids {[hex(i) for i in clip]}")
    copy = _u16s(img, 0x080EF288, 20)
    res.check(copy == want, f"Clip Pos type-0x4C copy {[hex(i) for i in copy]}")
    slicer = _u16s(img, SLICER_POS, 9)
    res.check(slicer == [0x8D, 0xF2, 0xD2, 0xF3, 0xF4, 0x4E, 0xA2, ID_REPITCH, 0],
              f"Slicer Pos ids {slicer}")


def t_deck_registry(bench, res):
    if bench.b.u32(REG_PTR) == 0:
        bench.b.call(PARAM_REGISTER_ALL, 0)
    for i, (xml, title) in enumerate((("cstart16", "Start:"), ("cend16", "End:"),
                                      ("cloopst16", "Loop Start:"), ("cloopend16", "Loop End:"))):
        name = bench.b.call(PARAM_XML_NAME, ID_DECK + i)
        t = bench.b.call(PARAM_TITLE, ID_DECK + i)
        res.check(name != 0 and bench.b.cstr(name) == xml, f"{ID_DECK + i:#x} xml {name:#x}")
        res.check(t != 0 and bench.b.cstr(t) == title, f"{ID_DECK + i:#x} title {t:#x}")


def t_deck_bag_defaults(bench, res):
    b = bench.b
    b.mu.mem_write(BAG_SCRATCH, b"\x00" * 0x40)
    b.w32(BAG_SCRATCH + 4, BAG_PAIRS)
    b.mu.mem_write(BAG_PAIRS, b"\x00" * 0x100)
    b.mu.mem_write(BAG_SCRATCH + 10, b"\x00\x00\x10\x00")
    b.call(bench.sym["deck_bag_defaults"], BAG_SCRATCH, 0)
    got = [b.call(PARAM_GET, BAG_SCRATCH, ID_DECK + i) for i in range(4)]
    res.check(got == [0, POS_END, 0, POS_END], f"defaults {got}")
    got = [b.call(PARAM_GET, BAG_SCRATCH, i) for i in (ID_GRID, ID_GRIDOFS)]
    res.check(got == [4, 0], f"Grid 1 bar, offset 0 {got}")
    b.call(PARAM_SET, BAG_SCRATCH, ID_DECK, 8)
    b.call(bench.sym["deck_bag_defaults"], BAG_SCRATCH, 1)
    res.check(b.call(PARAM_GET, BAG_SCRATCH, ID_DECK) == 8, "only-missing keeps a set Start")


def _u16(b, addr):
    return int.from_bytes(bytes(b.mu.mem_read(addr, 2)), "little")


def _deck_clip(b, unit_ticks=960, units=16, sync=1, loop=1, trigger=0):
    # deck_cue_note asks for the transport's tick (3.1.T late cue notes, FUN_08069C44: double precision,
    # which Unicorn's M7 lacks). These cases don't model the transport: always mid-bar, never late.
    b.stub(0x08069C44, lambda bb: bb.mu.mem_write(bb.arg(0), struct.pack("<q", 1500)))
    clip = 0x2001C800
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.w32(clip + 0x18, 0x0100)              # row 1, col 0
    b.mu.mem_write(clip + 0x1F4, bytes([loop, sync]))
    b.mu.mem_write(clip + 0x1F8, bytes([2 if trigger == 0 else 1]))
    b.w32(clip + 0x208, unit_ticks)
    b.w32(clip + 0x220, unit_ticks)
    b.w32(clip + 0x230, units * unit_ticks)
    b.w32(clip + 0x240, units)
    b.w32(clip + 0x1FC, 0x47000000)         # 32768.0 samples per unit
    return clip


def _s16(beats):
    """Bench values are written in beats; the params hold 1/16 notes, 512 = the old end-of-file value."""
    return POS_END if beats == 512 else beats * 4


def _deck_set(bench, clip, start, end, ls, le):
    for i, v in enumerate((start, end, ls, le)):
        bench.b.call(bench.sym["deck_store_unpacked"], 0x0100, ID_DECK + i, _s16(v))


def t_deck_region_engine(bench, res):
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    clip = _deck_clip(b)
    _deck_set(bench, clip, 0, 512, 0, 512)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "default: first unit 0")
    res.check(b.call(sym["deck_limit"], clip) == 16, "default: stock limit")
    b.call(sym["deck_rate"], clip)
    res.check(b.u32(clip + 0x230) == 16 * 960, "default: +0x230 untouched")

    _deck_set(bench, clip, 2, 14, 4, 8)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 2, "launch at Start")
    res.check(b.call(sym["deck_limit"], clip) == 8, "loop on: limit is Loop End")
    res.check(b.call(sym["deck_wrap_unit"], clip, 0) == 4, "wrap to Loop Start")
    b.mu.mem_write(clip + 0x1F4, b"\x00")   # Loop off for this call: with it on, in_loop takes a stray 0 back in
    res.check(b.call(sym["deck_wrap_unit"], clip, 0) == 0, "wrap target is one-shot")
    b.mu.mem_write(clip + 0x1F4, b"\x01")
    res.check(b.call(sym["deck_wrap_unit"], clip, 5) == 5, "non-wrap unit passes")
    b.call(sym["deck_rate"], clip)
    res.check(b.u32(clip + 0x230) == 960, "region: wrap re-syncs to one unit")
    res.check(_u16(b, DK_BASE + 0x40 + 4 * 16 + 12) == 960, "published unit ticks for row 1")

    b.mu.mem_write(clip + 0x1F4, b"\x00")
    res.check(b.call(sym["deck_limit"], clip) == 14, "loop off: limit is End")
    b.mu.mem_write(clip + 0x1F4, b"\x01")
    b.mu.mem_write(clip + 0x1F8, b"\x01")
    res.check(b.call(sym["deck_limit"], clip) == 14, "trigger mode: limit is End")

    _deck_set(bench, clip, 3, 512, 0, 512)
    b.mu.mem_write(clip + 0x1F8, b"\x02")
    res.check(b.call(sym["deck_limit"], clip) == 16, "loop markers follow End")
    b.call(sym["deck_limit"], clip)
    res.check(b.call(sym["deck_wrap_unit"], clip, 0) == 3, "loop markers follow Start")

    _deck_set(bench, clip, 6, 12, 9, 9)
    b.call(sym["deck_limit"], clip)
    res.check(b.call(sym["deck_wrap_unit"], clip, 0) == 6, "empty loop falls back to Start..End")

    clip = _deck_clip(b, unit_ticks=3840, units=8)   # Sync 1 bar: 4 beats per unit
    _deck_set(bench, clip, 6, 512, 9, 20)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 1, "beat 6 snaps to bar 1")
    res.check(b.call(sym["deck_limit"], clip) == 5, "Loop End beat 20 is bar 5")
    res.check(b.call(sym["deck_wrap_unit"], clip, 0) == 2, "Loop Start beat 9 is bar 2")

    clip = _deck_clip(b, sync=0)
    _deck_set(bench, clip, 2, 14, 4, 8)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "Sync None: whole file")
    clip = _deck_clip(b)
    b.w32(clip + 0x220, 0xF0000)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "Sync Slice: stock")
    clip = _deck_clip(b)
    b.mu.mem_write(clip + 0x254, b"\x01")
    res.check(b.call(sym["deck_note_unit"], clip, 5) == 5, "Slicer untouched")


def t_deck_long_file(bench, res):
    """End / Loop End at 512 (the default) mean the end of the file, also past beat 512 (device report:
    a 5-minute stem stopped or looped at beat 512 and only that part was drawn)."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    clip = _deck_clip(b, units=1000)          # Sync 1/4, 1000 beats
    _deck_set(bench, clip, 0, 512, 0, 512)
    res.check(b.call(sym["deck_limit"], clip, 600) == 1000, "defaults: limit is the whole file")
    res.check(b.call(sym["deck_active"], clip) == 0, "defaults: no region")
    b.call(sym["deck_rate"], clip)
    res.check(b.u32(clip + 0x230) == 1000 * 960, "defaults: +0x230 untouched")
    _deck_set(bench, clip, 8, 512, 16, 512)
    b.mu.mem_write(clip + 0x1F4, b"\x00")       # loop off: plays Start -> end of file
    res.check(b.call(sym["deck_limit"], clip, 600) == 1000, "End 512 on a long file: plays to the end")
    _deck_set(bench, clip, 8, 300, 16, 512)
    res.check(b.call(sym["deck_limit"], clip, 100) == 300, "an explicit End still cuts")
    clip = _deck_clip(b, unit_ticks=480, units=33)   # Sync 1/8, 16.5 beats = 66 1/16 notes
    for i, v in enumerate((0, 66, 0, 66)):
        b.call(sym["deck_store_unpacked"], 0x0100, ID_DECK + i, v)
    res.check(b.call(sym["deck_limit"], clip, 5) == 33, "End at the file's last 1/16 is the end")
    res.check(b.call(sym["deck_active"], clip) == 0, "... and is no region")
    b.call(sym["deck_store_unpacked"], 0x0100, ID_DECK + 1, 64)
    res.check(b.call(sym["deck_limit"], clip, 5) == 32, "End at beat 16 cuts the last half beat")
    _deck_set(bench, clip, 0, 8, 0, 16)
    res.check(b.call(sym["deck_limit"], clip, 5) == 16, "End 8 beats on 1/8 is unit 16")


def t_deck_bar_name(bench, res):
    """The Slicer-bar name (+0x1C48) is 9 characters on Clip too, so it leaves the Loop button alone."""
    img = bench.b.image
    got = (img[0x080A5594 - bd.BASE:0x080A5596 - bd.BASE].hex(), img[0x080A55A0 - bd.BASE:0x080A55A2 - bd.BASE].hex())
    want = ("0922", "1580") if "deck_ui_bind" in bench.sym else ("0c22", "1880")
    res.check(got == want, f"bar name strncpy / NUL {got} (want {want})")


def t_deck_clip_msg(bench, res):
    """Engine msg 0x39 for a region id lands in the SRAM4 table and returns without stock."""
    b = bench.b
    b.call(bench.sym["deck_init"])
    clip = _deck_clip(b)
    msg = 0x2001CD00
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(msg + 0xC, ID_DECK + 2)
    b.w32(msg + 0x10, 7)
    b.call(0x08067648, clip, 0x2001CC00, msg, 0)
    res.check(_u16(b, DK_BASE + 0x40 + 4 * 16 + 4) == 7, "Loop Start stored for row 1")


def _deck_stub_args(bench, res, hook, unit_in, unit_out, prep):
    b = bench.b
    seen = {}

    def on_grain(board):
        seen.update({k: board.arg(i) for i, k in enumerate(("r0", "r1", "r2", "r3", "a5", "a6", "a7"))})
        seen["lr"] = board.mu.reg_read(bd.A.UC_ARM_REG_LR)
        return 1

    b.stub(CLIP_STARTGRAIN, on_grain)
    clip = prep()
    sp = bd.BENCH_SP - 0x20
    for i, v in enumerate((0x51, 0x52, 0x53)):
        b.w32(sp + i * 4, v)
    A = bd.A
    for reg, v in ((A.UC_ARM_REG_R0, clip), (A.UC_ARM_REG_R1, 0x11), (A.UC_ARM_REG_R2, 0x12),
                   (A.UC_ARM_REG_R3, unit_in), (A.UC_ARM_REG_SP, sp), (A.UC_ARM_REG_LR, bd.STOP | 1)):
        b.mu.reg_write(reg, v)
    b.mu.emu_start(bench.sym[hook] | 1, bd.STOP, count=2000)
    b.unstub(CLIP_STARTGRAIN)
    res.check(seen.get("r0") == clip and seen.get("r1") == 0x11 and seen.get("r2") == 0x12,
              f"{hook} r0-r2 {seen}")
    res.check(seen.get("r3") == unit_out, f"{hook} unit {seen.get('r3')} (want {unit_out})")
    res.check([seen.get(k) for k in ("a5", "a6", "a7")] == [0x51, 0x52, 0x53], f"{hook} stack args")
    # 3.1.T: the next-grain stub goes on through the Mix line's env_slice, which calls the grain
    # start itself and then returns to our caller.
    via = bench.sym.get("env_slice_after") if hook == "hook_wrap_grain" and "str_stutter" in bench.sym else None
    res.check(seen.get("lr", 0) | 1 in (bd.STOP | 1, (via or 0) | 1), f"{hook} return address")
    res.check(b.mu.reg_read(A.UC_ARM_REG_SP) == sp, f"{hook} sp restored")


def t_deck_grain_stubs(bench, res):
    b = bench.b
    sym = bench.sym

    def prep():
        b.call(sym["deck_init"])
        clip = _deck_clip(b)
        _deck_set(bench, clip, 2, 14, 4, 8)
        return clip

    _deck_stub_args(bench, res, "hook_note_grain", 0, 2, prep)

    def prep_wrap():
        clip = prep()
        b.call(sym["deck_limit"], clip)
        return clip

    _deck_stub_args(bench, res, "hook_wrap_grain", 0, 4, prep_wrap)
    _deck_stub_args(bench, res, "hook_wrap_grain", 6, 6, prep_wrap)


def t_deck_limit_stub(bench, res):
    """hook_limit replaces ldrd r2,r3: r2 + r3 is the limit, r0/r1/r6/r8 survive."""
    b = bench.b
    b.call(bench.sym["deck_init"])
    clip = _deck_clip(b)
    _deck_set(bench, clip, 2, 14, 4, 8)
    A = bd.A
    # r8 is the unit about to start: inside the loop the limit is Loop End; past it (a cue jumped
    # there) the clip plays on to End (only when the cue table exists, 3.1.E P2).
    cases = [(7, 8)]
    if "deck_cue_note" in bench.sym:
        cases.append((9, 14))
    for r8, want in cases:
        for reg, v in ((A.UC_ARM_REG_R0, 0xA0), (A.UC_ARM_REG_R1, 0xA1), (A.UC_ARM_REG_R6, clip),
                       (A.UC_ARM_REG_R8, r8), (A.UC_ARM_REG_SP, bd.BENCH_SP), (A.UC_ARM_REG_LR, bd.STOP | 1)):
            b.mu.reg_write(reg, v)
        b.mu.emu_start(bench.sym["hook_limit"] | 1, bd.STOP, count=2000)
        got = [b.mu.reg_read(r) for r in (A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2,
                                          A.UC_ARM_REG_R3, A.UC_ARM_REG_R6, A.UC_ARM_REG_R8)]
        res.check(got[2] + got[3] == want, f"next {r8}: limit {got[2]} + {got[3]} (want {want})")
        res.check(got[0] == 0xA0 and got[1] == 0xA1 and got[4] == clip and got[5] == r8, f"regs {got}")
        res.check(b.mu.reg_read(A.UC_ARM_REG_SP) == bd.BENCH_SP, "sp restored")


# ---- 3.1.E P2: MIDI hot cues (docs/clip-deck.md, docs/map-319.md "MIDI note path")
ID_CUEMODE = 0x1A1
ID_CUENOTE = 0x1A2
ID_CUE1 = 0x1A3
ID_PADNOTE = 0xF5
PARAM_LABELS = 0x0808E278
PARAM_NLABEL = 0x0808E2A4
VOICE_START = 0x08065908
VOICE_SITES = (0x080683AE, 0x08068656, 0x080686CC)
CUE_BASE = DK_BASE + 0x140     # DkCue[16], 24 bytes: cue[8], pending, launch, note, mode, play
CUE_SWALLOW = 0x100
NONE16 = 0xFFFF
TICK_BUF = 0x2001CE00


def _bl_target(img, va):
    hw1, hw2 = (int.from_bytes(img[va - bd.BASE + i:va - bd.BASE + i + 2], "little") for i in (0, 2))
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0xD000:
        return None
    s = (hw1 >> 10) & 1
    j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
    o = (s << 24) | ((1 - (j1 ^ s)) << 23) | ((1 - (j2 ^ s)) << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
    if s:
        o -= 1 << 25
    return va + 4 + o


def _bw_target(img, va):
    """Target of a Thumb-2 b.w (T4) at va, or None."""
    hw1, hw2 = (int.from_bytes(img[va - bd.BASE + i:va - bd.BASE + i + 2], "little") for i in (0, 2))
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0x9000:
        return None
    s = (hw1 >> 10) & 1
    j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
    o = (s << 24) | ((1 - (j1 ^ s)) << 23) | ((1 - (j2 ^ s)) << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1)
    if s:
        o -= 1 << 25
    return va + 4 + o


def t_midi_path_facts(bench, res):
    """Stock MIDI note path the cue hook relies on (any image: none of these bytes may change)."""
    img = bench.b.image
    calls = [(0x08050B5C, 0x0804FE18, "map node: type 5 -> note-on router"),
             (0x0804FFFC, 0x0804F744, "router: padnote match -> pad event"),
             (0x08050068, 0x0804F744, "router: default note map -> pad event"),
             (0x08050B84, 0x0804F744, "map node: msg 0x42 -> pad event"),
             (0x08068C6A, 0x08067FA4, "clip node -> FUN_08067FA4")]
    for va, dest, what in calls:
        got = _bl_target(img, va)
        res.check(got == dest, f"{what} ({got and hex(got)})")
    # padnote / midimode stores into the 12-byte per-pad rows: str r1,[r3,#0x28] / [r3,#0x24]
    res.check(img[0x08050E56 - bd.BASE:0x08050E58 - bd.BASE].hex() == "9962", "padnote -> row +0x28")
    res.check(img[0x08050F30 - bd.BASE:0x08050F32 - bd.BASE].hex() == "5962", "midimode -> row +0x24")
    # router: padnote triggers pass note 0 (movs r3,#0 before the bl)
    res.check(img[0x0804FFF6 - bd.BASE:0x0804FFF8 - bd.BASE].hex() == "0023", "padnote trigger note 0")
    want = VOICE_START if "deck_cue_note" not in bench.sym else bench.sym["hook_voice"]
    for va in VOICE_SITES:
        got = _bl_target(img, va)
        res.check(got == want, f"voice start at {va:#x} -> {got and hex(got)}")


def t_cue_registry(bench, res):
    b = bench.b
    if b.u32(REG_PTR) == 0:
        b.call(PARAM_REGISTER_ALL, 0)
    names = [("cuemode", "Cue Mode:"), ("cuenote", "Cue Note:")] + \
            [(f"cueb{i + 1}", f"Cue {'ABCDEFGH'[i]}:") for i in range(8)] + \
            [("cgrid", "Grid:"), ("cgridofs", "Grid Offset:")]
    for i, (xml, title) in enumerate(names):
        n = b.call(PARAM_XML_NAME, ID_CUEMODE + i)
        t = b.call(PARAM_TITLE, ID_CUEMODE + i)
        res.check(n != 0 and b.cstr(n) == xml, f"{ID_CUEMODE + i:#x} xml {n and b.cstr(n)!r}")
        res.check(t != 0 and b.cstr(t) == title, f"{ID_CUEMODE + i:#x} title {t and b.cstr(t)!r}")

    def labels(pid):
        p = b.call(PARAM_LABELS, pid)
        return p, b.call(PARAM_NLABEL, pid)

    p, n = labels(ID_CUEMODE)
    res.check(n == 2 and [b.cstr(b.u32(p + 4 * i)) for i in range(2)] == ["Off", "On"], "Cue Mode Off/On")
    res.check(b.call(0x0808E2D0, ID_CUE1 + 3) == 0 and b.call(0x0808E304, ID_CUE1 + 3) == 4096,
              "cues are numbers 0..4096 (0 = Off, k = beat k - 1)")
    t, x = b.call(PARAM_TITLE, 0xD2), b.call(PARAM_XML_NAME, 0xD2)
    res.check(b.call(0x0808E2D0, 0xD2) == 0 and b.call(0x0808E304, 0xD2) == 1024 and b.cstr(t) == "Beat Count:"
              and b.cstr(x) == "beatcount", "Beat Count: a number 0..1024 (0 = Auto), same title and XML name")
    p, n = labels(ID_GRID)
    res.check(n == 9 and [b.cstr(b.u32(p + 4 * i)) for i in (0, 4, 8)] == ["1/16", "1 bar", "16 bars"],
              f"Grid sizes ({n})")
    p, n = labels(ID_CUENOTE)
    sp, sn = labels(ID_PADNOTE)
    res.check(n == 128 and p == sp and sn >= 128, f"Cue Note uses Pad Note names ({n}, {p:#x})")
    res.check(b.cstr(b.u32(p + 4 * 36)) == "C2", f"note 36 reads {b.cstr(b.u32(p + 4 * 36))!r}")


def t_cue_bag_defaults(bench, res):
    b = bench.b
    b.mu.mem_write(BAG_SCRATCH, b"\x00" * 0x40)
    b.w32(BAG_SCRATCH + 4, BAG_PAIRS)
    b.mu.mem_write(BAG_PAIRS, b"\x00" * 0x100)
    b.mu.mem_write(BAG_SCRATCH + 10, b"\x00\x00\x20\x00")
    b.call(bench.sym["deck_bag_defaults"], BAG_SCRATCH, 0)
    got = [b.call(PARAM_GET, BAG_SCRATCH, ID_CUEMODE + i) for i in range(10)]
    res.check(got == [0, 36, 1] + [0] * 7, f"cue defaults (Cue A at the start) {got}")
    b.call(PARAM_SET, BAG_SCRATCH, ID_CUE1 + 2, 9)
    b.call(bench.sym["deck_bag_defaults"], BAG_SCRATCH, 1)
    res.check(b.call(PARAM_GET, BAG_SCRATCH, ID_CUE1 + 2) == 9, "only-missing keeps a set cue")
    # Param_Set on a bag without the id inserts it (hook_set covers the whole deck range)
    b.mu.mem_write(BAG_PAIRS, b"\x00" * 0x100)
    b.mu.mem_write(BAG_SCRATCH + 10, b"\x00\x00\x20\x00")
    b.call(PARAM_SET, BAG_SCRATCH, ID_CUE1 + 7, 5)
    res.check(b.call(PARAM_GET, BAG_SCRATCH, ID_CUE1 + 7) == 5, "set inserts Cue 8")


def _cue(b, idx):
    raw = bytes(b.mu.mem_read(CUE_BASE + idx * 24, 24))
    u = [int.from_bytes(raw[i:i + 2], "little") for i in range(0, 20, 2)]
    return {"cue": u[:8], "pending": u[8], "launch": u[9], "note": raw[20], "mode": raw[21]}


def _cue_set(bench, key, mode=1, note=36, cues=(6,)):
    """cues: param values, 0 = Off, k = beat k - 1."""
    st = bench.sym["deck_store_unpacked"]
    bench.b.call(st, key, ID_CUEMODE, mode)
    bench.b.call(st, key, ID_CUENOTE, note)
    for i in range(8):
        bench.b.call(st, key, ID_CUE1 + i, cues[i] if i < len(cues) else 0)


def _tick(b, t):
    b.mu.mem_write(TICK_BUF, (t & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little"))
    return TICK_BUF


def t_cue_store(bench, res):
    """Param values reach the SRAM4 cue table from the UI post / engine 0x39 path."""
    b = bench.b
    b.call(bench.sym["deck_init"])
    res.check(_cue(b, 4) == {"cue": [NONE16] * 8, "pending": NONE16, "launch": NONE16, "note": 36, "mode": 0},
              f"init {_cue(b, 4)}")
    _cue_set(bench, 0x0100, mode=1, note=48, cues=(1, 0, 17, 513))
    c = _cue(b, 4)
    res.check(c["mode"] == 1 and c["note"] == 48, f"mode/note {c}")
    res.check(c["cue"][:4] == [0, NONE16, 64, 2048], f"cue 1/16 notes {c['cue'][:4]} (0 = unset)")
    clip = _deck_clip(b)
    msg = 0x2001CD00
    b.mu.mem_write(msg, b"\x00" * 0x18)
    b.w32(msg + 0xC, ID_CUE1 + 5)
    b.w32(msg + 0x10, 33)
    b.call(0x08067648, clip, 0x2001CC00, msg, 0)
    res.check(_cue(b, 4)["cue"][5] == 128, "engine msg 0x39 stores Cue 6 (beat 32 = 1/16 128)")
    res.check(_cue(b, 0)["mode"] == 0, "other pads untouched")
    b.w32(DK_BASE, 0x32434544)   # an older layout's magic ('DEC2'): the table must be cleared
    b.mu.mem_write(CUE_BASE + 4 * 24, b"\x55" * 24)
    b.call(bench.sym["deck_init"])
    res.check(_cue(b, 4)["cue"] == [NONE16] * 8 and _cue(b, 4)["note"] == 36, "an older magic re-inits the table")


def t_deck_loop_escape(bench, res):
    """Loop on: once in the loop the playhead stays in it, wherever the loop moves; starts and cue jumps
    may land outside it."""
    b = bench.b
    sym = bench.sym
    wrap, note, unit0 = sym["deck_wrap_unit"], sym["deck_cue_note"], sym["deck_note_unit"]
    b.call(sym["deck_init"])
    clip = _deck_clip(b)                      # one unit per beat, 16 units
    _deck_set(bench, clip, 0, 14, 4, 8)
    t = _tick(b, 960)
    res.check(b.call(unit0, clip, 0) == 0, "note start at Start, before the loop")
    res.check(b.call(wrap, clip, 2, t) == 2 and b.call(wrap, clip, 4, t) == 4, "plays into the loop")
    res.check(b.call(wrap, clip, 7, t) == 7, "inside the loop: unchanged")
    res.check(b.call(wrap, clip, 9, t) == 5, "Loop End moved behind the playhead: 9 -> 5")
    _deck_set(bench, clip, 0, 14, 8, 12)      # loop moved one bar later, playhead at 6
    res.check(b.call(wrap, clip, 7, t) == 11, "loop moved +1 bar: the playhead moves with it (7 -> 11)")
    _deck_set(bench, clip, 0, 14, 0, 4)       # and two bars back
    res.check(b.call(wrap, clip, 12, t) == 0, "loop moved -2 bars: 12 -> 0")
    _deck_set(bench, clip, 0, 14, 4, 8)
    b.mu.mem_write(clip + 0x1F4, b"\x00")
    res.check(b.call(wrap, clip, 9, t) == 9, "Loop off: unchanged")
    b.mu.mem_write(clip + 0x1F4, b"\x01")
    b.mu.mem_write(clip + 0x1F8, b"\x01")
    res.check(b.call(wrap, clip, 9, t) == 9, "Trigger mode: unchanged")
    b.mu.mem_write(clip + 0x1F8, b"\x02")
    b.call(wrap, clip, 5, t)
    _cue_set(bench, 0x0100, cues=(11,))       # Cue A at beat 10, past the loop
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(clip + 0x218, 0)                    # Quant None
    b.call(note, clip, 36)
    res.check(b.call(wrap, clip, 6, t) == 10, "cue jump past the loop lands")
    res.check(b.call(wrap, clip, 11, t) == 11 and b.call(wrap, clip, 12, t) == 12, "and plays on")
    res.check(b.call(wrap, clip, 5, t) == 5, "back in the loop")
    res.check(b.call(wrap, clip, 9, t) == 5, "then wraps again")
    _deck_set(bench, clip, 0, 512, 0, 512)
    res.check(b.call(wrap, clip, 9, t) == 9, "no region: stock")


def t_ui_wave_draw(bench, res):
    """The draw hook (vtable 0x080F1968) puts cue flags and the grid lines for what is on screen into the
    widget's marker buffer on every draw, so lines follow scrolling and zoom."""
    b = bench.b
    sym = bench.sym
    wave = VIEW + 0x38
    res.check(b.u32(0x080F1968) == sym["deck_wave_draw"] | 1, "vtable slot -> deck_wave_draw")
    v = WaveView(bench, units=400)
    v.set(ID_CUE1, 5)                         # Cue A beat 4
    v.marker(10)                              # 600 spp, centre px 159
    v.bar()
    ctx = 0xC1040000
    b.mu.mem_write(ctx, b"\x00" * 8)          # ctx[1] = 0: stock's draw returns at once
    buf = b.u32(wave + 0x184)
    b.call(sym["deck_wave_draw"], wave, ctx)
    n = b.u32(wave + 0x188)
    pos = [b.u32(buf + 4 * i) for i in range(n)]
    res.check(n > 3 and pos[0] == 4 * 48000 and b.u8(wave + 0x238) == 1, f"cue flag first, then lines ({n})")
    res.check(all(p % 48000 == 0 for p in pos[1:]) and 10 * 48000 in pos, f"a line per beat around the marker")
    v.marker(300)                             # scrolled far away, no screen update
    b.call(sym["deck_wave_draw"], wave, ctx)
    n = b.u32(wave + 0x188)
    pos = [b.u32(buf + 4 * i) for i in range(n)]
    res.check(300 * 48000 in pos and 10 * 48000 not in pos, "lines follow the scroll")
    b.mu.mem_write(wave + 0x168, struct.pack("<f", 48000.0 * 400 / 300))   # zoomed right out
    b.call(sym["deck_wave_draw"], wave, ctx)
    n = b.u32(wave + 0x188)
    pos = [b.u32(buf + 4 * i) for i in range(1, n)]
    res.check(n <= 128 and pos and all(p % (4 * 48000) == 0 for p in pos), f"zoomed out: bar lines ({n})")
    b.mu.mem_write(DK_UI + 3, b"\x00")
    v.bar()
    b.call(sym["deck_wave_draw"], wave, ctx)
    res.check(b.u32(wave + 0x188) == 1, "Lines off: the cue flag only")
    v.bar(mode=2, is_slicer=1)
    b.w32(wave + 0x188, 7)
    b.call(sym["deck_wave_draw"], wave, ctx)
    res.check(b.u32(wave + 0x188) == 7, "Slicer: stock markers untouched")


def t_edit_play_cursor(bench, res):
    """With the "..." editor open and the waveform drawn lately, a note start begins at the bar of the
    white marker."""
    b = bench.b
    sym = bench.sym
    wave = VIEW + 0x38
    v = WaveView(bench, units=16)
    v.bar()
    b.mu.mem_write(VIEW + 0x2F8 + 0x30, b"\x00")   # row open
    v.marker(9.5)
    b.w32(UI_TICK, 5000)
    ctx = 0xC1040000
    b.mu.mem_write(ctx, b"\x00" * 8)
    b.call(sym["deck_wave_draw"], wave, ctx)
    clip = _deck_clip(b)                      # row 1 col 0, 16 units of a beat
    _deck_set(bench, clip, 0, 512, 0, 512)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 8, "starts at the marker's bar (beat 9.5 -> 8)")
    b.mu.mem_write(VIEW + 0x2F8 + 0x30, b"\x01")
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "row closed: from the start")
    b.mu.mem_write(VIEW + 0x2F8 + 0x30, b"\x00")
    b.w32(UI_TICK, 6000)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "screen not drawn for 1 s: from the start")
    b.w32(UI_TICK, 5100)
    _deck_set(bench, clip, 12, 512, 0, 512)
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 12, "before Start: from Start")
    b.w32(clip + 0x18, 0x0101)                # another pad
    res.check(b.call(sym["deck_note_unit"], clip, 0) == 0, "another pad: stock")


def t_cue_engine(bench, res):
    b = bench.b
    sym = bench.sym
    note, wrap, limit, unit0 = sym["deck_cue_note"], sym["deck_wrap_unit"], sym["deck_limit"], sym["deck_note_unit"]
    b.call(sym["deck_init"])
    clip = _deck_clip(b)                      # Sync 1/4: one unit per beat, 16 units, row 1 col 0
    _cue_set(bench, 0x0100, cues=(6, 0, 101))  # Cue 1 beat 5, Cue 2 unset, Cue 3 beat 100 (past the end)

    res.check(b.call(note, clip, 35) == 35 and b.call(note, clip, 44) == 44, "notes outside the 8 pass")
    res.check(b.call(note, clip, 37) == 37, "stopped + unset cue: stock launch")
    res.check(b.call(note, clip, 36) == 0, "stopped + Cue 1: launch with note 0")
    res.check(_cue(b, 4)["launch"] == 5, "launch unit 5")
    res.check(b.call(unit0, clip, 0) == 5, "note start begins at the cue")
    res.check(_cue(b, 4)["launch"] == NONE16 and b.call(unit0, clip, 0) == 0, "launch is one-shot")

    b.mu.mem_write(clip + 0x28, b"\x01")     # playing, lane 0 active
    b.mu.mem_write(clip + 0x260, b"\x01")
    res.check(b.call(note, clip, 36) == CUE_SWALLOW and _cue(b, 4)["pending"] == 5, "playing: jump pending")
    res.check(b.call(note, clip, 37) == CUE_SWALLOW and _cue(b, 4)["pending"] == 5,
              "playing + unset cue: no retrigger, pending kept")
    res.check(b.call(note, clip, 44) == 44, "playing: notes outside the 8 still retrigger")

    b.w32(clip + 0x218, 3840)                 # Quant Size 1 bar
    res.check(b.call(wrap, clip, 3, _tick(b, 960 * 3)) == 3, "off the bar: keep playing")
    res.check(_cue(b, 4)["pending"] == 5, "still pending")
    res.check(b.call(wrap, clip, 4, _tick(b, 3840 * 7)) == 5, "on the bar: jump to the cue")
    res.check(_cue(b, 4)["pending"] == NONE16, "jump is one-shot")
    res.check(b.call(wrap, clip, 6, _tick(b, 3840 * 8)) == 6, "next bar plays on")
    if "deck_cue_late" in sym:
        late = sym["deck_cue_late"]
        res.check(b.call(late, clip, _tick(b, 3840 * 8)) == NONE16, "late take: nothing pending, nothing taken")
        b.call(note, clip, 36)
        res.check(b.call(late, clip, _tick(b, 3840 * 8 + 960)) == NONE16 and _cue(b, 4)["pending"] == 5,
                  "late take: a unit off the bar stays, the jump stays pending")
        res.check(b.call(late, clip, _tick(b, 3840 * 9)) == 5 and _cue(b, 4)["pending"] == NONE16,
                  "late take: the unit chosen on the bar becomes the cue")
    b.call(note, clip, 38)
    res.check(_cue(b, 4)["pending"] == 15, "cue past the file clamps to the last unit")
    res.check(b.call(wrap, clip, 7, _tick(b, -3840)) == 15, "negative tick on the grid jumps")
    b.call(note, clip, 36)
    b.w32(clip + 0x218, 0)                    # Quant None: the next grain
    res.check(b.call(wrap, clip, 9, _tick(b, 960 * 5 + 960)) == 5, "Quant None: next grain jumps")

    b.call(note, clip, 36)
    b.call(unit0, clip, 0)
    res.check(_cue(b, 4)["pending"] == NONE16, "a new note start drops the pending jump")

    _deck_set(bench, clip, 2, 14, 4, 8)       # region with a loop: a jump wins over the wrap
    b.w32(clip + 0x218, 3840)
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.call(note, clip, 36)
    b.call(limit, clip, 8)
    res.check(b.call(wrap, clip, 0, _tick(b, 3840 * 3)) == 5, "pending jump beats the loop wrap")
    b.call(limit, clip, 8)
    res.check(b.call(wrap, clip, 0, _tick(b, 3840 * 4)) == 4, "then the loop wraps as before")
    res.check(b.call(limit, clip, 9) == 14, "past Loop End: play on to End")
    res.check(b.call(limit, clip, 8) == 8, "inside the loop: Loop End")

    b.call(note, clip, 36)
    bench.b.call(sym["deck_store_unpacked"], 0x0100, ID_CUEMODE, 0)
    res.check(_cue(b, 4)["pending"] == NONE16, "Cue Mode off drops the pending jump")
    res.check(b.call(note, clip, 36) == CUE_SWALLOW, "Cue Mode off but cues set: cues still work")
    _cue_set(bench, 0x0100, mode=0, cues=())
    res.check(b.call(note, clip, 36) == 36, "Cue Mode off, no cues: stock")
    _deck_set(bench, clip, 0, 512, 0, 512)
    res.check(b.call(sym["deck_active"], clip) == 0, "no region, no cues: Smooth keeps its head")
    bench.b.call(sym["deck_store_unpacked"], 0x0100, ID_CUEMODE, 1)
    res.check(b.call(sym["deck_active"], clip) == 1, "Cue Mode on: stock grains for Smooth/Capi/Vari")

    clip = _deck_clip(b, sync=0)
    b.mu.mem_write(clip + 0x28, b"\x01\x00")
    res.check(b.call(note, clip, 36) == 36, "Sync None: stock")
    clip = _deck_clip(b)
    b.mu.mem_write(clip + 0x254, b"\x01")
    res.check(b.call(note, clip, 36) == 36, "Slicer: stock (note picks the slice)")
    clip = _deck_clip(b)
    b.w32(clip + 0x18, 0x0101)                # row 1 col 1: its own table, mode off
    res.check(b.call(note, clip, 36) == 36, "per pad: another pad is unaffected")


SAMPLE_SEEK_IMPL = 0x08074CE8   # provider vtable (0x080E9604) +0x10: frame >> 13 -> FUN_08074554
STREAM_VTABLE = 0x080E9604
STREAM_KICK = 0x08088402        # FUN_08074554 posts pcmStreamer's semaphore through this
DECK_REF = 0xC0500000                # fake sample ref (0x34 bytes): +0x20 provider, +0x24 wave id
PROV = 0xC0500100               # fake provider for the stubbed-seek case (vtable word only)
MGR = 0xC0100000                # fake streamer manager in SDRAM (mapped, unused by the bench)


def _cue_prefetch_clip(bench, provider, wave=7):
    b = bench.b
    b.call(bench.sym["deck_init"])
    clip = _deck_clip(b)                       # 32768 frames per unit, row 1 col 0
    b.mu.mem_write(DECK_REF, b"\x00" * 0x34)
    b.w32(DECK_REF + 0x20, provider)
    b.w32(DECK_REF + 0x24, wave)
    b.w32(clip + 0x24, DECK_REF)
    _cue_set(bench, 0x0100, cues=(6, 0, 13))    # Cue 1 beat 5, Cue 3 beat 12
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(clip + 0x218, 3840)                  # Quant Size 1 bar
    return clip


def t_cue_prefetch(bench, res):
    """A cue note on a playing clip asks for the target's audio at once and again on every grain until
    the jump; the playing grain and the clip are not touched and nothing reads the card."""
    b = bench.b
    sym = bench.sym
    seeks = []
    b.stub(SAMPLE_SEEK_IMPL, lambda bd_: seeks.append((bd_.arg(1), bd_.arg(2), bd_.arg(3))))
    b.mu.mem_write(PROV, b"\x00" * 0x5000)      # vtable word, empty request ring
    b.w32(PROV, STREAM_VTABLE)
    clip = _cue_prefetch_clip(bench, PROV)
    before = bytes(b.mu.mem_read(clip, 0xBB8))
    reads = b.reads
    res.check(b.call(sym["deck_cue_note"], clip, 36) == CUE_SWALLOW, "jump pending")
    res.check(seeks == [(7, 5 * 32768 - 8, 0)], f"seek at the cue on the note {seeks}")
    res.check(bytes(b.mu.mem_read(clip, 0xBB8)) == before, "clip and its lanes untouched")
    seeks.clear()
    res.check(b.call(sym["deck_wrap_unit"], clip, 3, _tick(b, 960 * 3)) == 3, "off the bar: plays on")
    res.check(seeks == [(7, 5 * 32768 - 8, 0)], f"re-touched while pending {seeks}")
    seeks.clear()
    b.w32(PROV + 0x4FFC, 40)                   # pcmStreamer busy: 40 requests waiting
    b.call(sym["deck_wrap_unit"], clip, 3, _tick(b, 960 * 5))
    res.check(seeks == [], "busy request ring: re-touch skipped")
    b.w32(PROV + 0x4FFC, 0)
    res.check(b.call(sym["deck_wrap_unit"], clip, 4, _tick(b, 3840 * 2)) == 5, "jump on the bar")
    res.check(seeks == [], "no extra seek at the jump (Clip_StartGrain makes its own)")
    b.call(sym["deck_cue_note"], clip, 37)
    res.check(seeks == [], "unset cue: no request")
    b.call(sym["deck_cue_note"], clip, 38)
    res.check(seeks == [(7, 12 * 32768 - 8, 0)], f"Cue 3 at beat 12 {seeks}")
    seeks.clear()
    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    b.call(sym["deck_cue_note"], clip, 36)
    res.check(seeks == [], "stopped clip: launch path, Clip_StartGrain seeks itself")
    b.w32(clip + 0x24, 0)
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.call(sym["deck_cue_note"], clip, 38)
    res.check(seeks == [], "no sample ref: no call")
    b.unstub(SAMPLE_SEEK_IMPL)
    res.check(b.reads == reads, "no card reads from the engine side")


def t_cue_prefetch_stock_seek(bench, res):
    """The stock seek the prefetch uses only queues block requests for pcmStreamer: run it against a
    fake manager with an empty cache and check the request ring and the semaphore post."""
    b = bench.b
    kicks = []
    b.stub(STREAM_KICK, lambda bd_: kicks.append(bd_.arg(0)))
    b.mu.mem_write(MGR, b"\x00" * 0x9000)
    b.w32(MGR, STREAM_VTABLE)
    b.mu.mem_write(MGR + 0x434C, b"\xff\xff" * 0x240)
    b.mu.mem_write(MGR + 0x434C + 7 * 2, b"\x00\x00")   # wave 7 -> slot 0
    slots = 0xC0200000
    b.mu.mem_write(slots, b"\x00" * 0x170)
    b.mu.mem_write(slots + 0x2C, b"\x01")              # slot in use
    b.w32(MGR + 0x4348, slots)
    blockmap = 0xC0300000
    b.mu.mem_write(blockmap, b"\xff\xff" * 1500)       # nothing cached
    b.w32(MGR + 0x219C * 4, blockmap)
    b.w32(MGR + 0x47D0, 0xC0400000)                     # semaphore handle (stubbed post)
    clip = _cue_prefetch_clip(bench, MGR)
    reads = b.reads
    b.call(bench.sym["deck_cue_note"], clip, 36)
    ring = [(b.u8(MGR + 0x47F8 + i * 16), b.u32(MGR + 0x47FC + i * 16), b.u32(MGR + 0x4800 + i * 16))
            for i in range(b.u32(MGR + 0x4FFC))]
    blk = (5 * 32768 - 8) >> 13
    res.check(ring == [(4, 7, blk), (4, 7, blk + 1), (4, 7, blk + 2)], f"block requests {ring}")
    res.check(len(kicks) == 3 and set(kicks) == {0xC0400000}, f"pcmStreamer woken {len(kicks)}x")
    b.call(bench.sym["deck_wrap_unit"], clip, 3, _tick(b, 960 * 3))
    n = b.u32(MGR + 0x4FFC)
    res.check(n <= 6, f"re-touch adds at most the 3 uncached blocks again ({n} queued; FUN_080740B4 skips cached ones)")
    b.unstub(STREAM_KICK)
    res.check(b.reads == reads, "no card reads from the engine side")


# ---- Song DJ row (3.1.T, firmware/patches/3.1.9-song-dj, docs/song-dj.md) ----

DJ = 0x38007400                 # DjState
DJ_UI = DJ + 0x420
DJ_BTN = DJ + 0x470
UI_TICK_AT = 0x2402EEE0
DJ_PLAYER = 0xC0700000          # fake sequence players and their Player_Tick frames (SDRAM)
DJ_SONG_P = 0xC0710000
DJ_CSP = 0xC0720000
DJ_APP = 0xC0730000
DJ_SONG_W = 0xC0800000          # fake Song widget (0x13000 bytes)
DJ_EV = 0xC0740000
ID_DJ = 0x1D0


def _dj_s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def _dj_ms(b, ms):
    b.w32(UI_TICK_AT, ms)


def _dj_jump(b, ticks):
    b.w32(DJ + 0x14, ticks)
    b.w32(DJ + 0x18, b.u32(UI_TICK_AT))
    b.w32(DJ + 0x10, b.u32(DJ + 0x10) + 1)


def _dj_loop(b, ticks):
    b.w32(DJ + 0x20, ticks)
    b.w32(DJ + 0x1C, b.u32(DJ + 0x1C) + 1)


def _dj_player(b, at, song=0, count=16, scene=0, scenes=3, moves=1, loop_all=0):
    b.mu.mem_write(at, b"\x00" * 0xD00)
    b.mu.mem_write(at + 0x30, b"\x01")
    b.mu.mem_write(at + 0x5C, bytes([moves, loop_all]))
    b.mu.mem_write(at + 0x60, bytes([song]))
    b.w32(at + 0x64, scenes)
    b.w32(at + 0xC20, at + 0x320)
    b.w32(at + 0x320 + 4, count)
    b.mu.mem_write(at + 0x320 + 0x40, scene.to_bytes(2, "little"))
    return at


def _dj_step(bench, p, step, count, tick, q):
    """dj_seq_step as hook_dj_step calls it: Player_Tick's frame holds the tick and the step length."""
    b = bench.b
    b.mu.mem_write(DJ_CSP, b"\x00" * 0x60)
    b.w32(DJ_CSP + 0x20, tick & 0xFFFFFFFF)
    b.w32(DJ_CSP + 0x14, (tick >> 32) & 0xFFFFFFFF)
    b.w32(DJ_CSP + 0x50, q)
    return _dj_s32(b.call(bench.sym["dj_seq_step"], p, step & 0xFFFFFFFF, count, DJ_CSP)) + 1


def t_dj_clip(bench, res):
    """Beatjump and autoloop on a Clip (1-beat units, 4 bars): the jump on the first beat past the
    request, phase kept, round the file; a loop wraps on its grid; a Clip started later stays out."""
    b = bench.b
    sym = bench.sym
    wrap = sym["deck_wrap_unit"]
    b.call(sym["deck_init"])
    res.check(b.u32(DJ) == 0x314A4453, "deck_init starts the DJ state")
    clip = _deck_clip(b)
    _dj_ms(b, 1000)
    _dj_jump(b, 3840)
    res.check(b.call(wrap, clip, 3, _tick(b, 2880)) == 3, "jump asked: plays on until the beat")
    res.check(b.u32(DJ + 0x24) == 1 and b.u32(DJ + 0x30) == 3840, "armed for the next beat (3840)")
    res.check(b.call(wrap, clip, 4, _tick(b, 3840)) == 8, ">> 1 bar on the beat: unit 4 -> 8")
    res.check(b.call(wrap, clip, 9, _tick(b, 4800)) == 9, "taken once")
    _dj_jump(b, -3840)
    b.call(wrap, clip, 10, _tick(b, 5760))
    res.check(b.call(wrap, clip, 11, _tick(b, 6720)) == 7, "<< 1 bar")
    _dj_jump(b, 61440)
    b.call(wrap, clip, 8, _tick(b, 7680))
    res.check(b.call(wrap, clip, 9, _tick(b, 8640)) == 9, "16 bars on a 4-bar clip: round to the same place")
    _dj_jump(b, 3840)
    _dj_ms(b, 2000)
    b.call(wrap, clip, 10, _tick(b, 9600))
    res.check(b.call(wrap, clip, 11, _tick(b, 10560)) == 11, "a jump nobody played for 300 ms is dropped")
    _dj_loop(b, 3840)
    res.check(b.call(wrap, clip, 2, _tick(b, 11520)) == 2, "loop on at bar 4")
    res.check(_dj_s32(b.u32(DJ + 0x40)) == 11520 and b.u32(DJ + 0x3C) == 3840, "loop window on the bar grid")
    got = [b.call(wrap, clip, u, _tick(b, 11520 + 960 * i)) for i, u in ((1, 3), (2, 4), (3, 5), (4, 6), (5, 3))]
    res.check(got == [3, 4, 5, 2, 3], f"1-bar loop: 3 4 5 back to 2 {got}")
    _dj_jump(b, 7680)
    b.call(wrap, clip, 4, _tick(b, 17280))
    res.check(b.call(wrap, clip, 5, _tick(b, 18240)) == 13, "a jump while looping moves the loop")
    got = [b.call(wrap, clip, u, _tick(b, 18240 + 960 * i)) for i, u in ((1, 14), (2, 11), (3, 12))]
    res.check(got == [10, 11, 12], f"... and it loops there: 13 10 11 12 {got}")
    _dj_loop(b, 0)
    got = [b.call(wrap, clip, u, _tick(b, 21120 + 960 * i)) for i, u in ((1, 13), (2, 14), (3, 15), (4, 0))]
    res.check(got == [13, 14, 15, 0], f"loop off: plays on from inside the loop {got}")
    _dj_loop(b, 3840)
    b.call(sym["deck_note_unit"], clip, 0)
    got = [b.call(wrap, clip, u, _tick(b, 26880 + 960 * i)) for i, u in ((0, 1), (1, 2), (2, 3), (3, 4), (4, 5))]
    res.check(got == [1, 2, 3, 4, 5], f"a Clip started after Loop on is not looped {got}")
    _dj_loop(b, 0)
    clip = _deck_clip(b, unit_ticks=3840, units=8)
    b.call(sym["deck_note_unit"], clip, 0)
    _dj_jump(b, 960)
    b.call(wrap, clip, 1, _tick(b, 3840 * 9))
    res.check(b.call(wrap, clip, 2, _tick(b, 3840 * 10)) == 2, "1-bar units: a 1-beat jump does nothing")


def t_dj_seq(bench, res):
    """Beatjump and autoloop on a pad sequence (1/16 steps, 64 of them)."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    p = _dj_player(b, DJ_PLAYER)
    layer, idx, pool, ptr = p + 0x320, DJ_PLAYER + 0x2000, DJ_PLAYER + 0x2100, DJ_PLAYER + 0x2400
    for i in range(16):                        # a note on every beat of the 4 bars
        b.mu.mem_write(idx + 2 * i, i.to_bytes(2, "little"))
        b.mu.mem_write(pool + 0x18 * i, b"\x01" + b"\x00" * 3 + (960 * i).to_bytes(4, "little") + b"\x00" * 16)
    b.w32(layer + 0xC, idx)
    b.w32(layer + 0x10, 16)
    b.w32(ptr, pool)
    b.w32(layer + 0x1C, ptr)
    _dj_ms(b, 1000)
    res.check(_dj_step(bench, p, 9, 64, 2400, 240) == 10, "no request: stock's next step")
    _dj_jump(b, 3840)
    res.check(_dj_step(bench, p, 10, 64, 2640, 240) == 11, "jump asked: not before the beat")
    res.check(_dj_step(bench, p, 11, 64, 2880, 240) == 28, ">> 1 bar on the beat: 12 -> 28")
    base = struct.unpack("<q", b.mu.mem_read(layer + 0x38, 8))[0]
    res.check(base == 2880 - 28 * 240 and b.u32(layer + 0x14) == 7,
              f"its notes follow: pass start {base}, next note #{b.u32(layer + 0x14)} (beat 8)")
    _dj_jump(b, -3840)
    _dj_step(bench, p, 28, 64, 3120, 240)
    res.check(_dj_step(bench, p, 30, 64, 3840, 240) == 15, "<< 1 bar")
    _dj_jump(b, -15360)
    _dj_step(bench, p, 15, 64, 4080, 240)
    res.check(_dj_step(bench, p, 16, 64, 4800, 240) == 17, "<< 4 bars on a 4-bar sequence: same step")
    res.check(_dj_step(bench, p, -1, 64, 5040, 240) == 0, "a sequence that is just starting is left alone")
    _dj_loop(b, 1920)
    res.check(_dj_step(bench, p, 4, 64, 7680, 240) == 5, "loop on")
    got = [_dj_step(bench, p, s, 64, 7680 + 240 * i, 240) for i, s in ((1, 5), (7, 11), (8, 12))]
    res.check(got == [6, 12, 5], f"1/2-bar loop wraps to its start {got}")
    res.check(b.u32(layer + 0x14) == 2, f"... and its notes go back too: next note #{b.u32(layer + 0x14)} (beat 2)")
    _dj_loop(b, 0)
    res.check(_dj_step(bench, p, 12, 64, 11760, 240) == 13, "loop off")
    p2 = _dj_player(b, DJ_PLAYER + 0x1000)
    _dj_jump(b, 960)
    _dj_step(bench, p, 13, 64, 11760, 240)
    res.check(_dj_step(bench, p2, 1, 3, 12480, 1280) == 2, "steps longer than the jump: not moved")


def t_dj_song(bench, res):
    """With Song playback on: the song moves with the jump inside its scene, a jump out of the scene
    is not taken (the scene it would reach is posted for the blink), the loop is cut on the scene's
    end, and a step the song is moved to is not a scene start."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    _dj_ms(b, 1000)
    s = _dj_player(b, DJ_SONG_P, song=1, count=16, scene=0, scenes=3)
    _dj_step(bench, s, 3, 16, 3840, 960)
    res.check(b.u32(DJ + 0x58) == s and b.u32(DJ + 0x5C) == 4 and b.u32(DJ + 0x68) == 16, "song player seen")
    _dj_jump(b, 61440)
    res.check(_dj_step(bench, s, 4, 16, 4800, 960) == 5, "16 bars in a 4-bar scene: not taken")
    res.check(b.u32(DJ + 0x50) == 1 and b.u8(DJ + 0x54) == 1 and b.u32(DJ + 0x28) == 0, "refused, scene 2 blinks")
    _dj_jump(b, 3840)
    _dj_step(bench, s, 5, 16, 5760, 960)
    res.check(_dj_step(bench, s, 6, 16, 6720, 960) == 11, ">> 1 bar inside the scene: 7 -> 11")
    res.check(b.u8(s + 0x60) == 0 and b.u8(DJ + 0x55) == 1, "moved: song flag off for this step")
    b.call(sym["dj_seq_done"], s)
    res.check(b.u8(s + 0x60) == 1 and b.u8(DJ + 0x55) == 0, "song flag back after the step's events")
    _dj_jump(b, -15360)
    _dj_step(bench, s, 11, 16, 7680, 960)
    res.check(b.u32(DJ + 0x50) == 2 and b.u8(DJ + 0x54) == 0, "<< 4 bars past the scene start: refused (first scene)")
    res.check(_dj_step(bench, s, 15, 16, 11520, 960) == 16, "the scene ends a bar early, as it moved on (stock wraps)")
    res.check(b.u8(s + 0x60) == 1 and b.u32(DJ + 0x68) == 0, "a real scene start: untouched, length unknown")
    b.w32(s + 0x320 + 4, 18)
    b.call(sym["dj_seq_done"], s)
    res.check(b.u32(DJ + 0x68) == 18, "the new scene's length once the layer switched")
    base = 11520
    for st in range(0, 16):
        _dj_step(bench, s, st, 18, base + 960 * (st + 1), 960)
    _dj_loop(b, 3840)
    res.check(_dj_step(bench, s, 16, 18, base + 960 * 17, 960) == 17, "loop on at beat 18 of 18")
    res.check(b.u32(DJ + 0x48) == 13440 and b.u32(DJ + 0x4C) == 17280, "1 bar cut on the scene's end: beats 15-18")
    res.check(_dj_step(bench, s, 17, 18, base + 960 * 18, 960) == 14, "scene end: back to beat 15, no scene change")
    res.check(b.u8(s + 0x60) == 0, "the loop's wrap is not a scene start")
    b.call(sym["dj_seq_done"], s)
    _dj_jump(b, 3840)
    _dj_step(bench, s, 14, 18, base + 960 * 19, 960)
    res.check(b.u32(DJ + 0x50) == 3, ">> while the loop sits at the scene's end: refused")
    _dj_jump(b, -3840)
    _dj_step(bench, s, 15, 18, base + 960 * 20, 960)
    res.check(_dj_s32(b.u32(DJ + 0x48)) == 9600 and b.u32(DJ + 0x4C) == 13440, "<< 1 bar moves the loop window")

    # Resizing at the scene's end: a 1/4 loop on the last beat, grown to 1 bar on a wrap, then shrunk.
    b.call(sym["deck_init"])
    s = _dj_player(b, DJ_SONG_P, song=1, count=16, scene=0, scenes=3)
    for st in range(-1, 14):
        _dj_step(bench, s, st, 16, 960 * (st + 1), 960) if st >= 0 else None
    _dj_step(bench, s, 14, 16, 14400, 960)
    _dj_loop(b, 960)
    def song_step(step, t):                    # as Player_Tick runs it: hook_dj_step, the events, hook_dj_step0
        r = _dj_step(bench, s, step, 16, t, 960)
        b.call(sym["dj_seq_done"], s)
        return r
    got = [song_step(15, t) for t in (15360, 16320)]
    res.check(got == [15, 15], f"1/4 loop on at the last beat: holds it {got}")
    _dj_loop(b, 3840)
    got = [song_step(15, 17280), song_step(15, 18240)]
    res.check(got == [15, 12] and b.u32(DJ + 0x48) == 11520 and b.u32(DJ + 0x4C) == 15360,
              f"grown to 1 bar on a wrap: the scene's last bar, no scene change {got}")
    song_step(12, 19200)
    _dj_loop(b, 960)
    got = [song_step(13, 20160), song_step(12, 21120)]
    res.check(got == [12, 12] and b.u32(DJ + 0x48) == 11520 and b.u32(DJ + 0x4C) == 12480,
              f"halved to 1/4 in beat 3: start kept, jumps into the first quarter {got} "
              f"window {b.u32(DJ + 0x48)}..{b.u32(DJ + 0x4C)}")


def t_dj_loop_mixxx(bench, res):
    """Autoloop as Mixxx: a loop starts on the closest beat (a catching loop when it is just ahead), a
    new size keeps the start, a halved loop jumps into its first half in whole sizes, a doubled one
    plays on (Song playback off; 1/16 steps)."""
    b = bench.b
    sym = bench.sym
    wrap = sym["deck_wrap_unit"]

    def seq_wrap_at():
        return struct.unpack("<q", b.mu.mem_read(DJ + 0x240 + 8, 8))[0]

    b.call(sym["deck_init"])
    _dj_ms(b, 1000)
    p = _dj_player(b, DJ_PLAYER)
    _dj_step(bench, p, 39, 512, 9600, 240)
    _dj_loop(b, 30720)
    res.check(_dj_step(bench, p, 40, 512, 9840, 240) == 41, "8 bars pressed 1/16 after beat 3 of bar 3: plays on")
    res.check(_dj_s32(b.u32(DJ + 0x40)) == 9600 and seq_wrap_at() == 9600 + 30720,
              f"... the loop starts on that beat, not on an 8-bar line (start {_dj_s32(b.u32(DJ + 0x40))})")
    res.check(_dj_step(bench, p, 167, 512, 40320, 240) == 40, "8 bars later: back to beat 3 of bar 3")

    b.call(sym["deck_init"])
    p = _dj_player(b, DJ_PLAYER)
    _dj_step(bench, p, 37, 512, 9120, 240)
    _dj_loop(b, 3840)
    res.check(_dj_step(bench, p, 38, 512, 9360, 240) == 39 and _dj_s32(b.u32(DJ + 0x40)) == 9600,
              "pressed 1/16 before a beat: the loop starts on that beat (catching)")
    res.check(_dj_step(bench, p, 39, 512, 9600, 240) == 40, "... the playhead runs into it")
    res.check(_dj_step(bench, p, 55, 512, 13440, 240) == 40, "... and a bar later loops back there")

    b.call(sym["deck_init"])
    p = _dj_player(b, DJ_PLAYER)
    _dj_step(bench, p, 63, 512, 15360, 240)
    _dj_loop(b, 61440)
    _dj_step(bench, p, 64, 512, 15600, 240)
    _dj_loop(b, 30720)
    res.check(_dj_step(bench, p, 239, 512, 57600, 240) == 112,
              "16 bars, halved in bar 12: jumps 8 bars back into the first half, same phase")
    res.check(_dj_s32(b.u32(DJ + 0x40)) == 15360 and seq_wrap_at() == 76800, "start kept, end at 8 bars")
    _dj_loop(b, 61440)
    res.check(_dj_step(bench, p, 127, 512, 61440, 240) == 128, "doubled again: plays on")
    res.check(seq_wrap_at() == 61440 - 15360 + 61440, f"... the end is 16 bars after the start ({seq_wrap_at()})")

    b.call(sym["deck_init"])
    clip = _deck_clip(b, unit_ticks=960, units=64)
    b.call(sym["deck_note_unit"], clip, 0)
    b.call(wrap, clip, 15, _tick(b, 14400))
    _dj_loop(b, 61440)
    b.call(wrap, clip, 16, _tick(b, 15360))
    _dj_loop(b, 30720)
    res.check(b.call(wrap, clip, 60, _tick(b, 57600)) == 28, "a Clip halved in bar 12 jumps the same 8 bars")


def t_dj_ui(bench, res):
    """SONG on the Song screen toggles the row; its buttons post jumps and the loop; the bottom knobs
    step the sizes; the row is built on the Song widget, labelled, and a refused jump blinks a row."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    b.mu.mem_write(DJ_APP, b"\x00" * 0x9000)
    b.mu.mem_write(DJ_APP + 0x8CA4, bytes([0x2D]))
    bench.screen.clear()
    b.call(sym["dj_song_key"], DJ_APP, 0x2D, 0, 0)
    res.check(b.u8(DJ_UI + 4) == 1 and bench.screen == [], "SONG on Song: row asked, no screen switch")
    b.call(sym["dj_song_key"], DJ_APP, 0x2D, 0, 0)
    res.check(b.u8(DJ_UI + 4) == 0, "SONG again: row hidden")
    b.mu.mem_write(DJ_APP + 0x8CA4, bytes([2]))
    b.call(sym["dj_song_key"], DJ_APP, 0x2D, 0, 0)
    res.check(bench.screen == [(DJ_APP, 0x2D)], "SONG elsewhere opens Song as stock")

    stock = []
    b.stub(0x080C3540, lambda bd_: stock.append(("child", bd_.u16(bd_.arg(1)))) or 1)
    b.stub(0x080C3860, lambda bd_: stock.append(("event", bd_.u16(bd_.arg(1)))))
    b.stub(0x080C26FC, lambda bd_: stock.append(("tick", 0)))

    def ev(kind, a, c=0):
        b.mu.mem_write(DJ_EV, b"\x00" * 0x20)
        b.mu.mem_write(DJ_EV, kind.to_bytes(2, "little"))
        b.w32(DJ_EV + 0xC, a)
        b.mu.mem_write(DJ_EV + 0x10, (c & 0xFFFF).to_bytes(2, "little"))
        return DJ_EV

    child, event, tick = sym["dj_song_child"], sym["dj_song_event"], sym["dj_song_tick"]
    _dj_ms(b, 4000)
    res.check(b.call(child, DJ_SONG_W, ev(1, ID_DJ + 1)) == 0 and b.u32(DJ + 0x10) == 1
              and _dj_s32(b.u32(DJ + 0x14)) == 61440 and b.u32(DJ + 0x18) == 4000, ">> press: +16 bars")
    b.call(child, DJ_SONG_W, ev(1, ID_DJ))
    res.check(_dj_s32(b.u32(DJ + 0x14)) == -61440 and b.u32(DJ + 0x10) == 2, "<< press: -16 bars")
    b.call(child, DJ_SONG_W, ev(2, ID_DJ))
    res.check(b.u32(DJ + 0x10) == 2 and stock == [], "release swallowed")
    b.call(child, DJ_SONG_W, ev(1, ID_DJ + 2))
    res.check(b.u8(DJ_UI + 8) == 1 and b.u32(DJ + 0x20) == 3840 and b.u32(DJ + 0x1C) == 1, "Loop: on, 1 bar")
    b.call(child, DJ_SONG_W, ev(1, 0x27))
    res.check(stock == [("child", 1)], "Song's own buttons reach stock")
    stock.clear()
    b.call(event, DJ_SONG_W, ev(0x32, 1, -400))
    res.check(stock == [("event", 0x32)] and b.u8(DJ_UI + 6) == 6, "row hidden: knobs are stock's")
    stock.clear()
    b.mu.mem_write(DJ_SONG_W, b"\x00" * 0x13000)
    for i in range(32):
        b.w32(DJ_SONG_W + 0x34 + i * 0x7F8 + 0x7A0, i)
    b.mu.mem_write(DJ_SONG_W + 0x12195, b"\x5a\xa5\x01")    # the mode is a byte: its neighbours are not ours
    b.mu.mem_write(DJ_UI + 4, b"\x01")
    b.call(tick, DJ_SONG_W, 0, 0, 0)
    deck = mixer = "mx_block" in bench.sym
    if deck:                                    # 3.1.T deck view: the crossfader is placed last
        res.check(b.u32(DJ_SONG_W + 0x18) == 0x38006F00 and b.u32(DJ_BTN + 0x2C) == DJ_SONG_W,
                  "deck widgets are the Song widget's oldest children (drawn last, hit first)")
        rects = [tuple(b.u32(DJ_BTN + i * 0xA0 + o) for o in (4, 8, 0xC, 0x10)) for i in range(3)]
        res.check(rects == [(0, 182, 43, 38), (84, 182, 43, 38), (200, 182, 120, 38)],
                  f"top bar: << and >> left (the size between them, 1 px gaps), Loop at the far right {rects}")
    else:
        res.check(b.u32(DJ_SONG_W + 0x18) == DJ_BTN + 2 * 0xA0 and b.u32(DJ_BTN + 0x2C) == DJ_SONG_W,
                  "buttons are the Song widget's oldest children (drawn last, hit first)")
        rects = [tuple(b.u32(DJ_BTN + i * 0xA0 + o) for o in (4, 8, 0xC, 0x10)) for i in range(3)]
        res.check(rects == [(0, 0, 99, 38), (100, 0, 99, 38), (200, 0, 120, 38)], f"row at the bottom {rects}")
    hid = [b.u8(DJ_BTN + i * 0xA0 + 0x30) for i in range(3)]
    texts = [b.cstr(DJ_BTN + i * 0xA0 + 0x65) for i in range(3)] + [b.cstr(DJ_UI + 0x20)]
    res.check(hid == [0, 0, 0] and texts == ["<<", ">>", "Loop 1", "16"],
              f"shown and labelled, the jump size once {hid} {texts}")
    res.check(b.u8(DJ_BTN + 2 * 0xA0 + 0x96) == 1, "Loop lit while on")
    res.check(stock == [("tick", 0)], "stock tick runs")
    kj, kl = (0, 2) if deck else (1, 3)        # deck view: jump top-left, loop size top-right
    b.call(event, DJ_SONG_W, ev(0x32, kj, -400))
    b.call(event, DJ_SONG_W, ev(0x32, kl, 300))
    b.call(event, DJ_SONG_W, ev(0x32, kl, 500))
    res.check(b.u8(DJ_UI + 6) == 5 and b.u8(DJ_UI + 7) == 6, "knobs: jump 8 bars, loop 4 bars (400 counts a step)")
    res.check(b.u32(DJ + 0x20) == 15360 and b.u32(DJ + 0x1C) == 2, "the live loop takes the new size")
    b.call(event, DJ_SONG_W, ev(0x32, kj, -400 * 20))
    res.check(b.u8(DJ_UI + 6) == 0, "jump size stops at 1/4 bar")
    if deck:                                    # bottom knobs: loop filter left, project filter right
        b.call(event, DJ_SONG_W, ev(0x32, 1, 400))
        b.call(event, DJ_SONG_W, ev(0x32, 3, -400))
        res.check(("event", 0x32) not in stock and b.u16(0x38007A60 + 0x2C) == 50
                  and b.u16(0x38007A60 + 0x2A) == (-50) & 0xFFFF,
                  "bottom knobs: loop filter (left), project filter (right), not stock's")
        b.call(sym["mx_set_filter"], 0, 0)
        b.call(sym["mx_set_filter"], 1, 0)
    else:
        b.call(event, DJ_SONG_W, ev(0x32, 0, 400))
        res.check(("event", 0x32) in stock, "top-left knob stays BPM")
    flags = 0x2001C780
    b.mu.mem_write(flags, b"\x00\x00")
    b.call(tick, DJ_SONG_W, flags, 0, 0)
    texts = [b.cstr(DJ_UI + 0x20 + 16 * i) for i in (0, 2)]
    res.check(texts == ["1/4", "Loop 4"], f"labels follow {texts}")
    if deck:
        res.check(bytes(b.mu.mem_read(flags, 2)) == b"\x01\x01",
                  f"a changed label: the tick asks the GUI for both passes {bytes(b.mu.mem_read(flags, 2)).hex()}")
        b.mu.mem_write(flags, b"\x00\x00")
        b.call(tick, DJ_SONG_W, flags, 0, 0)
        b.call(tick, DJ_SONG_W, flags, 0, 0)
        res.check(b.u8(flags) == 0, "nothing changed: no full pass asked")
    b.mu.mem_write(DJ_SONG_W + 0x12194, b"\x02")
    b.call(tick, DJ_SONG_W, 0, 0, 0)
    res.check(b.u8(DJ_BTN + 0x30) == 1, "a scene grid open: row hidden")
    b.mu.mem_write(DJ_SONG_W + 0x12194, b"\x00")
    b.call(tick, DJ_SONG_W, 0, 0, 0)
    b.mu.mem_write(DJ + 0x54, b"\x01")
    b.w32(DJ + 0x50, b.u32(DJ + 0x50) + 1)
    row1 = DJ_SONG_W + 0x34 + 0x7F8
    blinks = []
    for ms in (5000, 5050, 5150, 5250, 5450, 5700):
        _dj_ms(b, ms)
        b.call(tick, DJ_SONG_W, 0, 0, 0)
        blinks.append(b.u8(row1 + 0x7A4))
    res.check(blinks == [1, 1, 0, 1, 1, 0], f"refused jump: scene 2's row blinks, then back {blinks}")
    b.call(event, DJ_SONG_W, ev(0xD3, 0))
    res.check(b.u8(DJ_UI + 4) == 1 and b.u8(DJ_UI + 5) == 0 and b.u8(DJ_BTN + 0x30) == 1
              and b.u8(DJ_SONG_W + 0x1067C + 0x30) == 0,
              "leaving Song closes the deck at once (stock back), DJ mode remembered")
    b.mu.mem_write(DJ_UI + 0x20, b"\0")
    b.call(tick, DJ_SONG_W, 0, 0, 0)
    res.check(b.u8(DJ_UI + 5) == 1 and b.u8(DJ_BTN + 0x30) == 0 and b.cstr(DJ_UI + 0x20) == "1/4",
              "back on Song: the deck opens again, relabelled")
    b.mu.mem_write(DJ_APP + 0x8CA4, bytes([2]))
    b.call(sym["dj_song_key"], DJ_APP, 0x2D, 0, 0)
    res.check(b.u8(DJ_UI + 4) == 1, "SONG from another screen keeps DJ mode")
    for a in (0x080C3540, 0x080C3860, 0x080C26FC):
        b.unstub(a)


# ---- Song DJ mixer (3.1.T, firmware/patches/3.1.9-song-dj/mixer.c, docs/song-dj.md "Mixer") ----

MX = 0x38007A60                 # MxState
MX_SE = 0xC0900000              # fake sequencer engine: tempo +0x18, ticks +0x30, playing +0x38
MX_RING = 0xC1000000            # the fake sample cache's buffers (64 KB per entry)
MX_CACHE = 0x2400A9C0           # sample manager: 615 entries of 0x1C
MX_BUF = 0xC0980000             # one block, L then R
MX_FR = 70000                   # test signal: a saw this many frames long (not a loop multiple)


class _MxClock:
    """The engine's transport for the mixer: frames at 48 kHz, ticks (960 per beat) at a BPM."""

    def __init__(self, b, bpm=120.0):
        self.b, self.frame, self.tick, self.bpm = b, 0, 0.0, bpm
        b.w32(0x2400A9C0 + 0x9A74, MX_SE)
        b.mu.mem_write(MX_SE, b"\x00" * 0x40)
        self.set_bpm(bpm)
        b.mu.mem_write(MX_SE + 0x38, b"\x01")

    def set_bpm(self, bpm):
        self.bpm = bpm
        self.b.mu.mem_write(MX_SE + 0x18, struct.pack("<f", bpm))

    def tpf(self):
        return self.bpm * 960 / 60 / 48000

    def block(self, sym, left=None, right=0.25):
        """One master block through mx_block: returns (L, R) out. The project is the saw on L
        (by frame) and a constant on R."""
        b = self.b
        b.w32(MX_SE + 0x30, int(self.tick))
        b.w32(MX_SE + 0x34, 0)
        ls = [((self.frame + i) % MX_FR) / MX_FR - 0.5 if left is None else left(i) for i in range(128)]
        b.mu.mem_write(MX_BUF, struct.pack("<128f", *ls))
        b.mu.mem_write(MX_BUF + 512, struct.pack("<128f", *([right] * 128)))
        b.call(sym["mx_block"], MX_BUF, MX_BUF + 512, 128)
        self.frame += 128
        self.tick += 128 * self.tpf()
        return (struct.unpack("<128f", bytes(b.mu.mem_read(MX_BUF, 512))),
                struct.unpack("<128f", bytes(b.mu.mem_read(MX_BUF + 512, 512))))


def _mx_setup(bench):
    """A stock-like sample cache: 160 free entries with buffers, the rest loaded and old."""
    b = bench.b
    b.mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))    # FPU on
    b.call(bench.sym["deck_init"])
    for i in range(0x267):
        e = MX_CACHE + i * 0x1C
        free = i < 160
        # stock's fields run +0x04..+0x1F (they overlap the next entry's first word)
        b.mu.mem_write(e + 4, struct.pack("<IIIIIIHBB", MX_RING + i * 0x10000 if free else 0,
                                          MX_RING + i * 0x10000 + 0x8000 if free else 0, 0, 0, 0,
                                          0xFFFF, 0xFFFF, 0, 0 if free else 1))
    return _MxClock(b)


def _mx_cache_state(b, i):
    return b.u8(MX_CACHE + i * 0x1C + 0x1F)


def _mx_borrow(bench, clk, loop_len=3840, loop_a=0):
    """The DJ loop on: the audio task wants the ring, pcmStreamer's poll borrows it."""
    b = bench.b
    b.w32(DJ + 0x3C, loop_len)
    b.w32(DJ + 0x40, loop_a)
    b.w32(DJ + 0x44, 0)
    clk.block(bench.sym)
    b.call(bench.sym["mx_stream_poll"])


def _mx_s64(b, a):
    v = b.u32(a) | (b.u32(a + 4) << 32)
    return v - (1 << 64) if v >> 63 else v


def t_mx_loop(bench, res):
    """Capture, play, grow, load, repitch and clear: the loop is the DJ loop's last pass, read by
    transport tick from the ring; the crossfader goes left at W and right again on Loop off."""
    b = bench.b
    sym = bench.sym
    clk = _mx_setup(bench)
    res.check(b.u32(MX) == 0x31584D53 and b.u8(MX + 0x4C) == 0xE8, "mx_init: live, crossfader right")
    clk.block(sym)
    res.check(b.u32(MX + 0x50) == 0 and b.u32(MX + 0x58) == 0, "no DJ loop: no ring, nothing recorded")
    _mx_borrow(bench, clk, 3840, 7680)
    borrowed = [i for i in range(0x267) if _mx_cache_state(b, i) == 3]
    res.check(b.u32(MX + 0x50) == 2 and len(borrowed) == 131 and borrowed == list(range(131)),
              f"Loop on: 131 cache entries borrowed, reserved (state 3) {len(borrowed)}")
    w0 = b.u32(MX + 0x58)
    for _ in range(1499):                       # 4 s recorded
        lo, ro = clk.block(sym)
    res.check(b.u32(MX + 0x58) - w0 == 1499, f"ring records: {b.u32(MX + 0x58) - w0} blocks")
    res.check(abs(ro[5] - 0.25) < 1e-6, "no loop: the project passes untouched")
    w0 = b.u32(MX + 0x58)                       # the unit's audio task: 256-frame blocks
    b.w32(MX_SE + 0x30, int(clk.tick))
    b.mu.mem_write(0xC09A0000, struct.pack("<256f", *([0.1] * 256)))
    b.mu.mem_write(0xC09A1000, struct.pack("<256f", *([0.2] * 256)))
    b.call(sym["mx_block"], 0xC09A0000, 0xC09A1000, 256)
    clk.frame += 256
    clk.tick += 256 * clk.tpf()
    res.check(b.u32(MX + 0x58) == w0 + 2, f"a 256-frame block records as two ({b.u32(MX + 0x58) - w0})")
    ok, idx = b.call(sym["mx_ready"], 7680, 0, 3840), None
    res.check(ok == 1, "the pass playing now is recorded: BACK may capture")
    # the DJ loop: 1 bar from bar 2 (7680), switched off for the capture now
    t = int(clk.tick)
    b.w32(DJ + 0x1C, 2)
    b.w32(DJ + 0x38, 2)
    b.w32(DJ + 0x650, t & 0xFFFFFFFF)
    b.w32(DJ + 0x654, t >> 32)
    b.call(sym["mx_capture"], 7680, 0, 3840, 2)
    clk.block(sym)
    w = _mx_s64(b, MX + 0x78)
    exp_w = 7680 + max(1, -(-(t - 7680) // 3840)) * 3840
    res.check(b.u8(MX + 0x48) == 1 and w == exp_w, f"armed: W {w} = the DJ loop's first end after the off ({exp_w})")
    while clk.tick < w + 3 * 128 * clk.tpf():
        lo, ro = clk.block(sym)
    res.check(b.u8(MX + 0x48) == 2 and b.u16(MX + 0x4C) == 0, "held at W, crossfader fully left")
    a = w - 3840

    def expect(tick):
        d = (tick - a) % b.u32(MX + 0x68)
        fr = (a + d) / clk_tpf_rec
        return (fr % MX_FR) / MX_FR - 0.5
    clk_tpf_rec = clk.tpf()
    errs = []
    for _ in range(40):
        t0 = clk.tick
        lo, ro = clk.block(sym)
        for i in (3, 64, 120):
            e = expect(t0 + i * clk.tpf())
            if 0.02 < e + 0.5 < 0.98:            # away from the saw's edge
                errs.append(abs(lo[i] - e))
    res.check(errs and max(errs) < 0.004, f"plays the pass [W - 1 bar, W) by tick (max err {max(errs or [9]):.4f})")
    if "mx_eq" in sym:                          # the loop on its own into FX A, the project into B
        calls, _ = _mx_fx_rig(bench)
        b.call(sym["mx_set_route"], 0, 2)
        t0 = clk.tick
        b.call(sym["mx_eq"], MX_FXW, MX_SE)
        lo, ro = clk.block(sym)
        errs = [abs(lo[i] - (2 * expect(t0 + i * clk.tpf()) + 1)) for i in (3, 64, 120)
                if 0.02 < expect(t0 + i * clk.tpf()) + 0.5 < 0.98]
        res.check(errs and max(errs) < 0.01 and calls[0][1] != MX_BUF,
                  f"FX A on the held loop alone: 2 x loop + B's 1 on the silent project (max err {max(errs or [9]):.4f})")
        b.call(sym["mx_set_route"], 0, 1)
        lo, ro = clk.block(sym)                 # a plain block again for the checks below
    res.check(abs(ro[64] - 0.25) < 0.002, f"loop only on R {ro[64]:.4f}")
    wb, ws, wl = 0xC09C0000, 0xC09C0100, 0xC09C0108
    head = _dj_s32(b.call(sym["mx_wave_loop"], wb, 40, ws, wl))
    peaks = list(b.mu.mem_read(wb, 40))
    busy = b.u8(MX + 0x204)
    b.call(sym["mx_wave_done"])
    res.check(0 <= head < 40 and b.u32(wl) == 3840 and _mx_s64(b, ws) == a and max(peaks) > 60
              and busy == 1 and b.u8(MX + 0x204) == 0,
              f"waveform: loop lane peaks over [A, A+len), playhead {head}, busy while read {max(peaks)}")
    b.call(sym["mx_wave_proj"], wb, 40)
    res.check(min(b.mu.mem_read(wb, 40)) > 60, "waveform: project lane peaks (one per 4 parts)")
    pa = int(clk.tick) - 3 * 3840 + 1000              # a project loop of 1 bar, 3 passes ago
    head = _dj_s32(b.call(sym["mx_wave_pass"], wb, 40, pa & 0xFFFFFFFF, pa >> 32 & 0xFFFFFFFF, 3840))
    cols = list(b.mu.mem_read(wb, 40))
    res.check(0 <= head < 40 and min(cols) > 60,
              f"waveform: the project's loop over a whole pass from the peaks' ticks, playhead {head} {min(cols)}")
    pa = int(clk.tick) - 1000                         # started just now: nothing before its start
    head = _dj_s32(b.call(sym["mx_wave_pass"], wb, 40, pa & 0xFFFFFFFF, pa >> 32 & 0xFFFFFFFF, 3840))
    cols = list(b.mu.mem_read(wb, 40))
    res.check(max(cols[head + 1:]) == 0 and max(cols[:head + 1]) > 60,
              f"waveform: a first pass shows only what has played (head {head})")
    b.call(sym["mx_size"], 7680)
    clk.block(sym)
    early = b.u32(MX + 0x68)
    while clk.tick < a + 7680 + 8 * 128 * clk.tpf():
        clk.block(sym)
    res.check(early == 3840 and b.u32(MX + 0x68) == 7680, f"2 bars waits for the recording ({early} then {b.u32(MX + 0x68)}, "
              f"pend {b.u32(MX + 0x6C)} wblk {b.u32(MX + 0x58)} stop {b.u32(MX + 0x64)} a {a} t {clk.tick:.0f})")
    errs = []
    for _ in range(40):
        t0 = clk.tick
        lo, ro = clk.block(sym)
        e = expect(t0 + 64 * clk.tpf())
        if 0.02 < e + 0.5 < 0.98:
            errs.append(abs(lo[64] - e))
    res.check(errs and max(errs) < 0.004, f"2 bars: the bar that followed (max err {max(errs or [9]):.4f})")
    b.stub(0x0804C764, lambda bd_: 0)
    b.stub(0x08093E9C, lambda bd_: 1)
    b.call(sym["mx_load_begin"], 0x2400A9C0)
    clk.block(sym)
    stop = b.u32(MX + 0x64)
    for _ in range(50):
        clk.block(sym)
    res.check(b.u32(MX + 0x58) == stop and b.u8(MX + 0x2E) == 1, "a preset load stops the recording, BPM Global read")
    clk.set_bpm(140.0)
    for _ in range(4):
        clk.block(sym)
    errs = []
    for _ in range(60):
        t0 = clk.tick
        lo, ro = clk.block(sym)
        e = expect(t0 + 64 * clk.tpf())
        if 0.02 < e + 0.5 < 0.98:
            errs.append(abs(lo[64] - e))
    res.check(errs and max(errs) < 0.006, f"140 BPM: repitched, still on the grid (max err {max(errs or [9]):.4f})")
    b.call(sym["mx_size"], 1920)
    clk.block(sym)
    res.check(b.u32(MX + 0x68) == 1920, "half a bar: at once")
    b.call(sym["mx_set_xf"], 500)
    clk.block(sym)
    lo, ro = clk.block(sym)
    res.check(abs(ro[64] - 0.25 * 2) < 0.003, f"crossfader centre: both sides at full level ({ro[64]:.4f})")
    b.call(sym["mx_set_xf"], 350)              # left of the middle: the loop still full
    clk.block(sym)
    clk.block(sym)
    res.check(b.u32(MX + 0x9C) == 0x3F800000 and 0 < struct.unpack("<f", struct.pack("<I", b.u32(MX + 0xA0)))[0] < 1,
              "left of the middle: loop at full level, the project fading in")
    clk.block(sym)
    b.call(sym["mx_set_xf"], 40)
    clk.block(sym)
    lo, ro = clk.block(sym)
    res.check(abs(ro[64] - 0.25) < 0.002 and b.u32(MX + 0x9C + 4) == 0, f"4%: still in the dead zone, project silent ({ro[64]:.4f})")
    b.call(sym["mx_set_xf"], 80)
    clk.block(sym)
    clk.block(sym)
    res.check(b.u32(MX + 0x9C + 4) != 0, "8%: past the dead zone, the project starts")
    b.call(sym["mx_set_xf"], 960)
    clk.block(sym)
    lo, ro = clk.block(sym)
    res.check(b.u32(MX + 0x9C) == 0, "96%: the loop fully down")
    b.call(sym["mx_clear"])
    clk.block(sym)
    lo, ro = clk.block(sym)
    res.check(b.u8(MX + 0x48) == 0 and b.u16(MX + 0x4C) == 1000 and abs(ro[64] - 0.25) < 1e-6,
              "Loop off: no loop, crossfader right, the project alone")
    w0 = b.u32(MX + 0x58)
    clk.block(sym)
    res.check(b.u32(MX + 0x58) == w0 + 1, "and the ring records again (the DJ loop is still on)")
    b.w32(DJ + 0x3C, 0)                         # DJ loop off too: the ring goes back
    clk.block(sym)
    b.call(sym["mx_stream_poll"])
    res.check(b.u32(MX + 0x50) == 3, "draining once nothing wants it")
    for _ in range(3):
        clk.block(sym)
    b.call(sym["mx_stream_poll"])
    states = {_mx_cache_state(b, i) for i in range(131)}
    res.check(b.u32(MX + 0x50) == 0 and states == {0}
              and all(b.u32(MX_CACHE + i * 0x1C + 0x18) == 0xFFFF for i in range(131)),
              f"given back: the 131 entries free again {states}")


def t_mx_noring(bench, res):
    """Without a ring (not taken), a playing transport only passes the project through: nothing
    reads the ring's tables (a null read faulted on the unit when PLAY was pressed)."""
    b = bench.b
    sym = bench.sym
    clk = _mx_setup(bench)
    b.w32(DJ + 0x3C, 3840)                      # wanted, but pcmStreamer has not borrowed it yet
    for _ in range(20):
        lo, ro = clk.block(sym)
    res.check(abs(ro[64] - 0.25) < 1e-6 and b.u32(MX + 0x58) == 0 and b.u8(MX + 0x202) == 1,
              "playing, ring wanted but not borrowed: project untouched, nothing recorded")
    b.call(sym["mx_capture"], 0, 0, 3840, 0)
    for _ in range(5):
        lo, ro = clk.block(sym)
    res.check(b.u8(MX + 0x48) == 0, "a capture without a ring does nothing")


def t_mx_filters(bench, res):
    """The DJ filters: low-pass left of centre, high-pass right of it, off at the centre."""
    b = bench.b
    sym = bench.sym
    clk = _mx_setup(bench)
    alt = lambda i: 0.5 if i % 2 == 0 else -0.5
    b.call(sym["mx_set_filter"], 0, -950)
    for _ in range(200):
        lo, ro = clk.block(sym, left=alt)
    rms = (sum(x * x for x in lo) / 128) ** 0.5
    res.check(rms < 0.02, f"LP: Nyquist taken out (rms {rms:.4f})")
    res.check(abs(ro[100] - 0.25) < 0.01, f"LP: DC through ({ro[100]:.4f})")
    b.call(sym["mx_set_filter"], 0, 950)
    for _ in range(60):
        lo, ro = clk.block(sym, left=alt)
    res.check(abs(ro[100]) < 0.01, f"HP: DC taken out ({ro[100]:.4f})")
    res.check(abs(abs(lo[100]) - 0.5) < 0.1, f"HP: Nyquist through ({lo[100]:.4f})")
    b.call(sym["mx_set_filter"], 0, 0)
    for _ in range(20):
        lo, ro = clk.block(sym, left=alt)
    res.check(lo[10] == 0.5 and ro[10] == 0.25, "centre: off, untouched")


def t_mx_bpm(bench, res):
    """BPM Global: a loaded preset's BPM is kept (session bag set to the playing tempo), Swing goes
    through; Project: both go through."""
    b = bench.b
    sym = bench.sym
    _mx_setup(bench)
    sent, set_ = [], []
    b.stub(0x0804C59C, lambda bd_: sent.append((bd_.arg(2), bd_.arg(3))) or 0)
    b.stub(0x08093ECC, lambda bd_: set_.append((bd_.arg(0), bd_.arg(1), bd_.arg(2))) or 0)
    send = sym["mx_bpm_send"]
    b.call(send, 0x2400A9C0, 0x10400, 0x84, 140, 0, 0)
    res.check(sent == [(0x84, 140)] and set_ == [], "Project: the preset's BPM goes to the engine")
    b.mu.mem_write(MX + 0x2E, b"\x01")
    sent.clear()
    b.call(send, 0x2400A9C0, 0x10400, 0x84, 140, 0, 0)
    b.call(send, 0x2400A9C0, 0x10400, 0xD7, 60, 0, 0)
    res.check(sent == [(0xD7, 60)], f"Global: BPM kept, Swing sent {sent}")
    res.check(set_ == [(0x240230C0, 0x84, 120)], f"... and the session bag shows the playing 120 {set_}")


def t_mx_ui(bench, res):
    """Song screen with the row: BACK captures (DJ loop off, capture posted), top knobs set the
    filters, the loop knob sizes both loops, Loop loops the project, BACK erases the audio loop."""
    b = bench.b
    sym = bench.sym
    _mx_setup(bench)
    b.stub(0x080C3860, lambda bd_: 0)
    b.stub(0x080C3540, lambda bd_: 1)

    def ev(kind, a=0, c=0):
        b.mu.mem_write(DJ_EV, b"\x00" * 0x20)
        b.mu.mem_write(DJ_EV, kind.to_bytes(2, "little"))
        b.w32(DJ_EV + 0xC, a)
        b.mu.mem_write(DJ_EV + 0x10, (c & 0xFFFF).to_bytes(2, "little"))
        return DJ_EV
    event, child = sym["dj_song_event"], sym["dj_song_child"]
    clk = _MxClock(b)
    _mx_borrow(bench, clk, 3840, 11520)
    for _ in range(20):
        clk.block(sym)
    b.mu.mem_write(DJ_UI + 5, b"\x01")           # shown
    b.call(child, DJ_SONG_W, ev(1, ID_DJ + 2))    # Loop on
    b.w32(DJ + 0x38, b.u32(DJ + 0x1C))            # armed by the audio task
    b.w32(DJ + 0x3C, 3840)
    b.w32(DJ + 0x40, 11520)
    seq = b.u32(DJ + 0x1C)
    b.mu.mem_write(DJ_APP, b"\x00" * 0x9000)
    b.mu.mem_write(DJ_APP + 0x8CA4, bytes([2]))
    res.check(b.call(sym["dj_back_key"], DJ_APP) == 1 and b.u32(MX + 0x04) == 0, "BACK off the Song screen: stock's")
    b.mu.mem_write(DJ_APP + 0x8CA4, bytes([0x2D]))
    b.call(sym["mx_set_filter"], 0, 300)
    b.call(sym["mx_set_filter"], 1, -500)
    took = b.call(sym["dj_back_key"], DJ_APP)
    res.check(b.u16(MX + 0x2A) == 0 and b.u16(MX + 0x2C) == 0, "a capture resets both filters")
    res.check(took == 0 and b.u32(MX + 0x04) == 1 and b.u32(MX + 0x0C) == 3840 and b.u32(MX + 0x10) == 11520,
              "hardware BACK (App_HandleInput type 7) with Loop on: taken, capture of the DJ loop posted")
    res.check(b.u8(DJ_UI + 8) == 0 and b.u32(DJ + 0x20) == 0 and b.u32(DJ + 0x1C) == seq + 1
              and b.u32(MX + 0x08) == seq + 1, "... and the DJ loop switched off by that request")
    b.mu.mem_write(MX + 0x48, b"\x02")
    b.call(event, DJ_SONG_W, ev(0x32, 1, 80))
    b.call(event, DJ_SONG_W, ev(0x32, 3, -800))
    res.check(b.u16(MX + 0x2C) == 10 and b.u16(MX + 0x2A) == (-100) & 0xFFFF,
              "bottom knobs: loop filter left +10, project filter right -100")
    b.w32(MX + 0x68, 3840)
    b.call(event, DJ_SONG_W, ev(0x32, 2, 400))
    res.check(b.u32(MX + 0x1C) == 1 and b.u32(MX + 0x20) == 7680, "loop knob: the audio loop to 2 bars")
    b.call(child, DJ_SONG_W, ev(1, ID_DJ + 2))
    seq = b.u32(DJ + 0x1C)
    res.check(b.u32(MX + 0x18) == 0 and b.u8(DJ_UI + 8) == 1 and b.u32(DJ + 0x20) == 7680,
              "Loop with an audio loop: the project loops (the shared size), the audio loop stays")
    b.call(event, DJ_SONG_W, ev(0x32, 2, -400))
    res.check(b.u32(MX + 0x1C) == 2 and b.u32(MX + 0x20) == 3840 and b.u32(DJ + 0x20) == 3840
              and b.u32(DJ + 0x1C) == seq + 1, "loop knob: both loops to 1 bar together")
    b.call(child, DJ_SONG_W, ev(1, ID_DJ + 2))
    took = b.call(sym["dj_back_key"], DJ_APP)
    res.check(took == 0 and b.u32(MX + 0x18) == 1 and b.u32(MX + 0x04) == 1 and b.u8(DJ_UI + 8) == 0,
              "BACK with an audio loop: erased, nothing captured")
    res.check(b.u16(MX + 0x2A) == 0xFF9C and b.u16(MX + 0x2C) == 10, "filters as set before")
    posted = []
    b.stub(0x08093E9C, lambda bd_: 4)                       # four scenes
    b.stub(0x080AED14, lambda bd_: posted.append((bd_.u16(bd_.arg(1)), bd_.u32(bd_.arg(1) + 0xC))) or 0)
    b.w32(0x24020088 + 0x20, 1)
    b.mu.mem_write(DJ_SONG_W + 0x12194, b"\x00")
    r1 = b.call(child, DJ_SONG_W, ev(1, 0x1DC))
    b.call(child, DJ_SONG_W, ev(2, 0x1DC))
    b.call(child, DJ_SONG_W, ev(1, 0x1DB))
    b.w32(0x24020088 + 0x20, 3)
    b.call(child, DJ_SONG_W, ev(1, 0x1DC))
    r2 = b.call(child, DJ_SONG_W, ev(1, 0x115))
    res.check(r1 == 0 and posted == [(0x107, 2), (0x107, 0)],
              f"arrows: v = next, ^ = previous (a row press, on the press), none past the last {posted}")
    res.check(r2 == 1, "stock's Loop1 is stock's again")
    b.call(child, DJ_SONG_W, ev(1, 0x1DF))        # Loop caption: the loop filter out
    off1 = b.u8(MX + 0xF6 + 1)
    b.call(child, DJ_SONG_W, ev(1, 0x1E0))        # Proj caption: the project filter out
    b.call(child, DJ_SONG_W, ev(1, 0x1DF))        # Loop again: back in
    res.check(off1 == 1 and b.u8(MX + 0xF6) == 1 and b.u8(MX + 0xF6 + 1) == 0,
              "Loop / Proj toggle their filter out and in")
    b.call(sym["mx_set_filter_on"], 0, 1)


MX_FXW = 0xC0960000             # fake EQ node, vtables, DJ FX screen widget


def _mx_fx_rig(bench):
    """A fake EQ node whose next stage is the DJ FX container in the fake engine, with two slots.
    FX A doubles its input, FX B adds 1.0 (so on silence it is a 'tail'). Returns (calls, eq_seen)."""
    b = bench.b
    cont, bus = MX_SE + 0x28D90, MX_SE + 0x150E4
    b.mu.mem_write(MX_FXW, b"\x00" * 0x400)
    b.mu.mem_write(cont, b"\x00" * 0x190)
    b.w32(MX_FXW, MX_FXW + 0x100)                       # node: vt +0xC = the EQ process (stubbed)
    b.w32(MX_FXW + 8, cont)
    b.w32(MX_FXW + 0x10C, 0x08047601)
    b.w32(cont, MX_FXW + 0x300)                         # container: vt +0xC = stock's process
    b.w32(MX_FXW + 0x30C, 0x0806DD0D)
    for k in range(2):
        b.w32(cont + 0x20 + k * 0x2C, MX_FXW + 0x200)   # slot: vt +0xC = stock's slot process
    b.w32(MX_FXW + 0x20C, 0x0806E191)
    b.mu.mem_write(cont + 0x180, struct.pack("<H", 2))
    b.mu.mem_write(bus, struct.pack("<IIIIBB", 128, 128, MX_BUF, MX_BUF + 512, 0, 1))
    calls, eq_seen = [], []
    b.stub(0x08047600, lambda bd_: eq_seen.append(bd_.u32(bd_.arg(0) + 8)) or 1)
    b.stub(0x0806DD0C, lambda bd_: calls.append(("stock", 0, 0)) or 1)

    def fx(bd_):
        k = (bd_.arg(0) - cont - 0x20) // 0x2C
        n, lp, rp = bd_.u32(bus + 4), bd_.u32(bus + 8), bd_.u32(bus + 12)
        for p in (lp, rp):
            xs = struct.unpack(f"<{n}f", bytes(bd_.mu.mem_read(p, 4 * n)))
            if p == lp:
                calls.append((k, p, round(xs[5], 4)))
            bd_.mu.mem_write(p, struct.pack(f"<{n}f", *[x * 2 if k == 0 else x + 1.0 for x in xs]))
        return 1
    b.stub(0x0806E190, fx)
    return calls, eq_seen


def t_mx_fx(bench, res):
    """The DJ FX behind the compressor: mx_eq runs the master EQ node without its next stage (the FX
    container), and the tap runs FX A then FX B on what the DJ FX screen routes to each."""
    b = bench.b
    sym = bench.sym
    clk = _mx_setup(bench)
    calls, eq_seen = _mx_fx_rig(bench)
    cont, bus = MX_SE + 0x28D90, MX_SE + 0x150E4
    bus0 = bytes(b.mu.mem_read(bus, 0x14))
    proj = lambda i: 0.1

    def block():
        calls.clear()
        eq_seen.clear()
        b.call(sym["mx_eq"], MX_FXW, MX_SE)
        return clk.block(sym, left=proj)

    res.check(b.u8(MX + 0x4E) == 1 and b.u8(MX + 0x4F) == 1, "boot: Proj into FX A and FX B (stock's sound)")
    lo, ro = block()
    res.check(eq_seen == [0] and b.u32(MX_FXW + 8) == cont, f"EQ ran without its next stage, put back {eq_seen}")
    res.check([c[0] for c in calls] == [0, 1] and all(c[1] == MX_BUF for c in calls),
              f"Proj, Proj: A then B on the project in place {calls}")
    res.check(abs(lo[5] - 1.2) < 1e-5 and abs(ro[5] - 1.5) < 1e-5, f"... (0.1 x 2 + 1 = {lo[5]:.4f}, R {ro[5]:.4f})")
    res.check(bytes(b.mu.mem_read(bus, 0x14)) == bus0, "bus 0 put back after the slots")
    b.call(sym["mx_set_route"], 0, 2)
    lo, ro = block()
    res.check(calls[0][1] != MX_BUF and calls[0][2] == 0 and calls[1][1] == MX_BUF and abs(calls[1][2] - 0.1) < 1e-5,
              f"A Loop, B Proj: A on the loop (silent), B on the project {calls}")
    res.check(abs(lo[5] - 1.1) < 1e-5, f"... project skips A: 0.1 + 1 ({lo[5]:.4f})")
    b.call(sym["mx_set_route"], 0, 0)
    b.call(sym["mx_set_route"], 1, 0)
    lo, ro = block()
    res.check(all(c[1] != MX_BUF and c[2] == 0 for c in calls) and len(calls) == 2,
              f"nothing routed: both run on silence {calls}")
    res.check(abs(lo[5] - 1.1) < 1e-5 and abs(ro[5] - 1.25) < 1e-5,
              f"... the project dry, B's tail added ({lo[5]:.4f}, {ro[5]:.4f})")
    b.call(sym["mx_set_route"], 0, 3)
    b.call(sym["mx_set_route"], 1, 2)
    lo, ro = block()
    res.check(all(c[1] == MX_BUF for c in calls) and abs(calls[1][2] - 0.2) < 1e-5,
              f"A both, B Loop: mixed at A, B takes the mix {calls}")
    res.check(abs(lo[5] - 1.2) < 1e-5, f"... ({lo[5]:.4f})")
    calls.clear()
    clk.block(sym, left=proj)
    res.check(calls == [], "no mx_eq this block (stock ran the FX): none run again")
    b.mu.mem_write(cont + 0x182, b"\x01")
    lo, ro = block()
    res.check(calls == [] and abs(lo[5] - 0.1) < 1e-5, f"container bypassed: no slot runs {calls}")
    b.mu.mem_write(cont + 0x182, b"\x00")
    calls.clear()
    b.call(sym["mx_eq"], MX_FXW, MX_SE)
    b.call(sym["mx_block"], 0xC09A0000, 0xC09A1000, 640)
    res.check(calls == [("stock", 0, 0)], f"a block too long for the buffers: the FX as stock {calls}")
    b.call(sym["mx_set_route"], 0, 1)
    b.call(sym["mx_set_route"], 1, 1)


def t_dj_fx(bench, res):
    """The DJ FX screen: Hold, Loop, Proj evenly in each half, the toggles routing into FX A / B; the
    bottom knobs are the filters, all four stock's with INFO held; Hold's A / B line blank."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    scr, flags = MX_FXW + 0x1000, MX_FXW + 0x3000
    b.mu.mem_write(scr, b"\x00" * 0x1000)
    b.mu.mem_write(flags, b"\x00" * 4)
    b.call(0x080A3F9D, scr + 0x3C)                      # stock's Hold buttons (their class)
    b.call(0x080A3F9D, scr + 0x134)
    stock = []
    b.stub(0x080AEB44, lambda bd_: stock.append(("tick", 0)))
    b.stub(0x080B1EE4, lambda bd_: stock.append(("child", bd_.u32(bd_.arg(1) + 0xC))) or 1)
    b.stub(0x080B1E9C, lambda bd_: stock.append(("event", bd_.u16(bd_.arg(1)))))

    def ev(kind, a, c=0):
        b.mu.mem_write(DJ_EV, b"\x00" * 0x20)
        b.mu.mem_write(DJ_EV, kind.to_bytes(2, "little"))
        b.w32(DJ_EV + 0xC, a)
        b.mu.mem_write(DJ_EV + 0x10, (c & 0xFFFF).to_bytes(2, "little"))
        return DJ_EV

    def toggles():
        out, w = {}, b.u32(scr + 0x18)
        while w:
            if b.u16(w + 0x14) >= 0x1E4:
                out[b.u16(w + 0x14)] = (tuple(_dj_s32(b.u32(w + 4 + 4 * i)) for i in range(4)), b.u8(w + 0x96),
                                        b.u8(w + 0x30), b.cstr(w + 0x65, 8))
            w = b.u32(w + 0x20)
        return out

    tick, child, event = sym["dj_fx_tick"], sym["dj_fx_child"], sym["dj_fx_event"]
    b.call(tick, scr, flags, 0, 0)
    t = toggles()
    want = {0x1E4: ((0, 118, 64, 30), 1, 0, "Proj"), 0x1E5: ((0, 153, 64, 30), 0, 0, "Loop"),
            0x1E6: ((0, 0, 64, 30), 1, 0, "Proj"), 0x1E7: ((0, 35, 64, 30), 0, 0, "Loop")}
    res.check(t == want, f"first tick: four toggles under the Hold buttons, Proj lit {t}")
    hold = [tuple(_dj_s32(b.u32(scr + o + 4 + 4 * i)) for i in range(4)) for o in (0x3C, 0x134)]
    res.check(hold == [(0, 188, 64, 30), (0, 70, 64, 30)],
              f"Hold over Loop over Proj, up to the counter (218), Hold B likewise below {hold}")
    res.check(stock == [("tick", 0)] and b.u8(flags) == 1 and b.u8(flags + 1) == 1, "stock tick runs, both passes asked")
    b.call(tick, scr, flags, 0, 0)
    res.check(len(toggles()) == 4, "built once")
    stock.clear()
    res.check(b.call(child, scr, ev(1, 0x1E5)) == 0 and b.call(sym["mx_route"], 0) == 3, "Loop A pressed: loop into FX A too")
    res.check(b.call(child, scr, ev(2, 0x1E5)) == 0 and b.call(sym["mx_route"], 0) == 3 and stock == [], "release swallowed")
    b.call(child, scr, ev(1, 0x1E6))
    res.check(b.call(sym["mx_route"], 1) == 0, "Proj B pressed: nothing into FX B")
    b.call(tick, scr, flags, 0, 0)
    t = toggles()
    res.check([t[i][1] for i in range(0x1E4, 0x1E8)] == [1, 1, 0, 0], f"lit as routed {[t[i][1] for i in range(0x1E4, 0x1E8)]}")
    stock.clear()
    b.call(child, scr, ev(2, 0x97))
    res.check(stock == [("child", 0x97)], "Hold A reaches stock")
    stock.clear()
    b.mu.mem_write(0x24009BDE, b"\x01")                 # INFO up
    f0 = b.call(sym["mx_filter"], 1)
    b.call(event, scr, ev(0x32, 1, 80))
    b.call(event, scr, ev(0x32, 3, -80))
    b.call(event, scr, ev(0x32, 2, 400))
    res.check(stock == [] and _dj_s32(b.call(sym["mx_filter"], 1)) == f0 + 10 and _dj_s32(b.call(sym["mx_filter"], 0)) == -10,
              "knobs: bottom-left loop filter, bottom-right project filter, top ones rest")
    b.mu.mem_write(0x24009BDE, b"\x00")                 # INFO held
    b.call(event, scr, ev(0x32, 3, 400))
    res.check(stock == [("event", 0x32)] and _dj_s32(b.call(sym["mx_filter"], 0)) == -10, "INFO held: the knob is stock's (FX swap)")
    b.mu.mem_write(0x24009BDE, b"\x01")
    stock.clear()
    b.call(event, scr, ev(0x66, 0))
    res.check(stock == [("event", 0x66)], "other events reach stock")
    blank = [b.cstr(b.u32(a), 4) for a in (0x080B2458, 0x080B245C)]
    res.check(blank == ["", ""], f"Hold's second line (A / B) is blank {blank}")
    b.call(sym["mx_set_route"], 0, 1)
    b.call(sym["mx_set_route"], 1, 1)


def t_master_row(bench, res):
    """Master sits in the FX row next to EQ: its button opens Tools page 4, where the row replaces
    the Tools tabs; the other Tools pages keep them; INFO's Tools cycle skips Master."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    tools, flags = 0xC0600000, MX_FXW + 0x3000
    b.mu.mem_write(tools, b"\x00" * 0xA600)
    b.call(0x080C4E71, tools + 0x9E28)                  # the tab bar (stock class)
    stock, shown = [], []
    b.stub(0x080C52E0, lambda bd_: stock.append(("event", bd_.u16(bd_.arg(1)))))
    b.stub(0x080C5360, lambda bd_: stock.append(("child", bd_.u32(bd_.arg(1) + 0xC))) or 1)
    b.stub(0x080AEB44, lambda bd_: None)
    b.stub(0x0809EAEC, lambda bd_: shown.append(bd_.arg(1)))

    def ev(kind, a=0):
        b.mu.mem_write(DJ_EV, b"\x00" * 0x20)
        b.mu.mem_write(DJ_EV, kind.to_bytes(2, "little"))
        b.w32(DJ_EV + 0xC, a)
        return DJ_EV

    def row():
        out, w = [], b.u32(tools + 0x18)
        while w:
            if 0x7A0 <= b.u16(w + 0x14) < 0x7A4:
                out.append((b.cstr(w + 0x65, 10), _dj_s32(b.u32(w + 4)), b.u32(w + 8), b.u32(w + 0xC),
                            b.u32(w + 0x10), b.u8(w + 0x96), b.u8(w + 0x30)))
            w = b.u32(w + 0x20)
        return sorted(out, key=lambda r: r[1])

    b.mu.mem_write(tools + 0xA5E0, b"\x00")
    b.call(sym["dj_tools_event"], tools, ev(0xD4))
    r = row()
    res.check(stock == [("event", 0xD4)] and len(r) == 4 and all(x[6] == 1 for x in r)
              and b.u8(tools + 0x9E28 + 0x30) == 0, f"Tools Main: tabs shown, the FX row hidden {r}")
    app_scr = 0x24020088 + 0x8CA4
    b.mu.mem_write(app_scr, b"\x29")                   # the FX row's Master: App_SetScreen(0x29)
    b.call(sym["dj_tools_event"], tools, ev(0xD4))
    res.check(b.u8(tools + 0xA5E0) == 4, f"shown as screen 0x29: the Master page (compressor), not Main ({b.u8(tools + 0xA5E0)})")
    b.call(sym["dj_tools_tick"], tools, flags, 0, 0)
    r = row()
    want = [("A Dly", 0, 0, 80, 38, 0, 0), ("B Rvb", 80, 0, 80, 38, 0, 0),
            ("EQ", 160, 0, 80, 38, 0, 0), ("Master", 240, 0, 80, 38, 1, 0)]
    res.check(r == want and b.u8(tools + 0x9E28 + 0x30) == 1, f"Master page: the FX row instead of the tabs, Master lit {r}")
    res.check(len(row()) == 4, "built once")
    stock.clear()
    res.check(b.call(sym["dj_tools_child"], tools, ev(1, 0x7A1)) == 0 and shown == [0x23], f"B Reverb: the Return page {shown}")
    shown.clear()
    b.call(sym["dj_tools_child"], tools, ev(1, 0x7A2))
    res.check(shown == [0x36], f"EQ: the EQ page {shown}")
    shown.clear()
    res.check(b.call(sym["dj_tools_child"], tools, ev(1, 0x7A3)) == 0 and shown == [] and stock == [], "Master: stays")
    b.call(sym["dj_tools_child"], tools, ev(1, 0x130))
    res.check(stock == [("child", 0x130)], "Tools' own presses reach stock")
    shown.clear()
    b.call(sym["fxret_child_event"], tools, ev(1, 0x7A3))
    res.check(shown == [0x29], f"Return / EQ page: Master opens Tools page 4 {shown}")
    b.mu.mem_write(app_scr, b"\x25")                   # Tools from the menu, Master left over
    b.call(sym["dj_tools_event"], tools, ev(0xD4))
    res.check(b.u8(tools + 0xA5E0) == 0 and b.u8(tools + 0x9E28 + 0x30) == 0,
              f"Tools opened otherwise: Main with its tabs ({b.u8(tools + 0xA5E0)})")
    b.mu.mem_write(tools + 0xA5E0, b"\x02")
    b.call(sym["dj_tools_event"], tools, ev(0xD4))
    res.check(b.u8(tools + 0xA5E0) == 2, "another Tools page is kept")
    res.check(b.cstr(b.u32(0x080EEE90), 8) == "" and bytes(b.mu.mem_read(0x080A30B6, 2)) == b"\x25\x21",
              "Tools tabs: no Master; INFO after Clock goes back to Main")


def t_dj_gui(bench, res):
    """Real GUI + App: SONG on the Song screen shows the DJ row (buttons on the Song widget, shown),
    SONG again hides it, SONG from Pads opens Song as stock."""
    b = bench.b
    mu = b.mu
    mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    b.unstub(SET_SCREEN)
    b.stub(0x080A2258, lambda board: 0)            # App_Update: the bench preset state resets screens
    tick = [0]

    def gettick(board):
        tick[0] += 5
        board.w32(UWTICK, tick[0])
        return tick[0]
    b.stub(0x0807794C, gettick)
    for fn in GUI_DP_STUBS + GUI_DRAW_STUBS:
        b.stub(fn, lambda board: 0)
    b.call(bench.sym["deck_init"])
    gui = 0xC2000000
    mu.mem_write(gui, b"\0" * 0x100000)
    b.call(0x0808BE4C, gui)
    b.w32(0x24002E6C, gui)
    for off, typ in ((0x2FC0, 0x26), (0x2FD8, 0x4B), (0x2FF0, 0x36), (0x3008, 0)):
        mu.mem_write(0x2001C800, bytes([typ]))
        b.call(0x08094A1C, APP_OBJ + off, 0x2001C800)
    ev = 0x2001C600
    song = gui + 0x59008

    def pump():
        for _ in range(3):
            b.call(0x0808B654, gui)
            b.call(0x0808B2D0, gui, limit=50_000_000)

    def mode_button(i):
        mu.mem_write(ev, b"\0" * 0x40)
        mu.mem_write(ev, struct.pack("<H", 0xF9))
        b.w32(ev + 0xC, i)
        b.call(0x080A2E60, APP_OBJ, ev)

    def where():
        return b.u8(APP_OBJ + 0x8CA4), b.u32(gui + 0x280 + 0x34) - gui
    mu.mem_write(APP_OBJ + 0x8CA4, b"\x02")
    mu.mem_write(APP_OBJ + 0x8CC3, b"\x00")
    b.call(SET_SCREEN, APP_OBJ, 2, 0, 0)
    pump()
    mode_button(3)
    pump()
    res.check(where() == (0x2D, 0x59008), f"SONG from Pads: Song screen {[hex(x) for x in where()]}")
    res.check(b.u32(song) == 0x080F1758, f"Song vtable {b.u32(song):#x}")
    res.check(b.u32(DJ) == 0x314A4453, f"DJ state live {b.u32(DJ):#x}")
    res.check(b.u8(DJ_UI + 4) == 0 and b.u8(DJ_UI + 5) == 0, "row not shown yet")
    res.check(b.u32(DJ_UI) == song, f"row built on the Song widget (tick ran) {b.u32(DJ_UI):#x}")
    from unicorn import UC_HOOK_CODE
    drawn = []

    def on_draw(mu_, addr, size, data):
        w = mu_.reg_read(bd.A.UC_ARM_REG_R0)
        if DJ_BTN <= w < DJ_BTN + 3 * 0xA0:
            ctx = mu_.reg_read(bd.A.UC_ARM_REG_R1)
            drawn.append(((w - DJ_BTN) // 0xA0, bytes(mu_.mem_read(ctx, 2)).hex()))
    mu.hook_add(UC_HOOK_CODE, on_draw, begin=0x080A3B6C, end=0x080A3B6C)
    leds = []
    mu.hook_add(UC_HOOK_CODE, lambda mu_, a, s_, d: leds.append((mu_.reg_read(bd.A.UC_ARM_REG_R1),
                                                                   mu_.reg_read(bd.A.UC_ARM_REG_R2))),
                begin=0x080436F8, end=0x080436F8)
    mode_button(3)
    pump()
    res.check({0, 1, 2} <= {w for w, ctx in drawn if ctx == "0001"}, f"all three drawn in the partial pass {drawn[:6]}")
    res.check((3, 1) in leds or (3, 0) in leds, f"SONG LED driven while the row is shown {leds[-4:]}")
    leds.clear()
    for i in range(4):
        _dj_ms(b, 10000 + 260 * i)
        pump()
    res.check({(3, 0), (3, 1)} <= set(leds), f"SONG LED blinks {sorted(set(leds))}")
    res.check(where() == (0x2D, 0x59008), "SONG on Song: stays on Song")
    res.check(b.u8(DJ_UI + 4) == 1 and b.u8(DJ_UI + 5) == 1, "row shown")
    res.check(all(b.u8(DJ_BTN + i * 0xA0 + 0x30) == 0 for i in range(3)), "buttons visible")
    if "mx_block" in bench.sym:                 # 3.1.T deck view
        deck = b.u32(0x38006F00 + 0xA4)
        widgets = [deck + i * 0xA0 for i in range(10)] + [DJ_BTN + i * 0xA0 for i in range(3)] + [0x38006F00]
        res.check(0xC0000000 <= deck < 0xC4000000, f"deck memory from SDRAM {deck:#x}")
        pump()
        relabels = []
        hrel = mu.hook_add(UC_HOOK_CODE, lambda mu_, a_, s_, d: relabels.append(mu_.reg_read(bd.A.UC_ARM_REG_R0)),
                           begin=0x080A3DEC, end=0x080A3DEC)
        for _ in range(3):
            pump()
        mu.hook_del(hrel)
        res.check(not relabels, f"no label set again while nothing changed {[hex(w) for w in relabels[:4]]}")
        res.check(b.u8(song + 0x1067C + 0x30) == 1 and b.u8(song + 0xFFE4 + 0x30) == 1,
                  "stock's scene list and top bar hidden under the deck")
        res.check(all(b.u8(w + 0x30) == 0 for w in widgets), "every deck widget shown")
        rects = [tuple(_dj_s32(b.u32(w + o)) for o in (4, 8, 0xC, 0x10)) for w in widgets[1:]]
        clash = [(r1, r2) for i, r1 in enumerate(rects) for r2 in rects[i + 1:]
                 if r1[0] < r2[0] + r2[2] and r2[0] < r1[0] + r1[2] and r1[1] < r2[1] + r2[3] and r2[1] < r1[1] + r1[3]]
        res.check(not clash and all(0 <= r[0] and r[0] + r[2] <= 320 and 0 <= r[1] and r[1] + r[3] <= 221 for r in rects),
                  f"deck widgets don't overlap, inside 320 x 221 {clash[:2]}")
        fills = []
        hfill = mu.hook_add(UC_HOOK_CODE, lambda mu_, a, s_, d: fills.append(1), begin=0x0808EA22, end=0x0808EA22)
        for w in (deck + 4 * 0xA0, deck + 7 * 0xA0, deck + 8 * 0xA0):   # waveform, both sliders
            b.mu.mem_write(w + 0x31, b"\x01\x01")
        b.call(bench.sym["mx_set_filter"], 1, -600)
        pump()
        mu.hook_del(hfill)
        res.check(len(fills) > 10, f"waveform and sliders painted with stock's FILL_RECT ({len(fills)} fills)")
        sl0 = deck + 7 * 0xA0                    # DjDeck: panel up dn line wave cap0 cap1 sl0 sl1
        pt = 0x2001C700
        b.mu.mem_write(pt, struct.pack("<ii", 8 + 80, 60))                  # right end of the loop slider
        b.call(b.u32(b.u32(sl0) + 0x10), sl0, pt)
        far = b.u16(0x38007A60 + 0x2C)
        b.mu.mem_write(pt, struct.pack("<ii", 8 + 37, 60))                  # 1 px off its centre (track 72 px)
        b.call(b.u32(b.u32(sl0) + 0x10), sl0, pt)
        res.check(far == 1000 and b.u16(0x38007A60 + 0x2C) == 0,
                  f"loop slider: right end = 1000, near the centre snaps Off ({far}, {b.u16(0x38007A60 + 0x2C)})")
    from unicorn import UC_HOOK_MEM_WRITE
    marked = set()

    def on_write(mu_, acc, addr, size, value, data):
        if size == 1 and value == 1:
            marked.add(addr)
    snap = {}
    def walk_snap(c):
        stack = [c]
        while stack:
            c = stack.pop()
            while c:
                if song <= c < song + 0x13000:
                    snap[c] = bytes(b.mu.mem_read(c + 0x31, 6))
                if b.u32(c + 0x18):
                    stack.append(b.u32(c + 0x18))
                c = b.u32(c + 0x20)
    walk_snap(b.u32(song + 0x18))
    hw = mu.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=song, end=song + 0x13000)
    mode_button(3)
    pump()
    mu.hook_del(hw)
    res.check(b.u8(DJ_UI + 5) == 0 and b.u8(DJ_BTN + 0x30) == 1, "SONG again hides it")
    if "mx_block" in bench.sym:
        tree, todo = [], [b.u32(song + 0x18)]
        while todo and len(tree) < 5000:
            c = todo.pop()
            while c and len(tree) < 5000:
                if song <= c < song + 0x13000:
                    tree.append(c)
                if b.u32(c + 0x18):
                    todo.append(b.u32(c + 0x18))
                c = b.u32(c + 0x20)
        labels = [w for w in tree if b.u32(w) == 0x080F17A4]
        spoilt = [w for w in labels if bytes(b.mu.mem_read(w + 0x31, 6)) != snap.get(w)]
        res.check(len(labels) > 5 and not spoilt,
                  f"closing the deck leaves every stock label's text alone ({len(labels)}, spoilt {len(spoilt)})")
        tree = [w for w in tree if b.u32(w) != 0x080F17A4]
        missed = [w for w in tree if w + 0x31 not in marked]
        res.check(len(tree) > 100 and not missed,
                  f"closing the deck marks every widget of the Song tree to repaint ({len(tree)}, missed {len(missed)})")
    if "mx_block" in bench.sym:
        res.check(b.u8(song + 0x1067C + 0x30) == 0 and b.u8(song + 0xFFE4 + 0x30) == 0,
                  "stock's scene list and top bar back")


def t_cue_voice_stub(bench, res):
    """hook_voice keeps r0-r2, the two stack args, lr and sp; r3 is the note it passes on."""
    b = bench.b
    sym = bench.sym
    A = bd.A
    seen = []

    def on_voice(board):
        seen.append({k: board.arg(i) for i, k in enumerate(("r0", "r1", "r2", "r3", "a5", "a6"))})
        seen[-1]["lr"] = board.mu.reg_read(A.UC_ARM_REG_LR)
        return 0

    b.stub(VOICE_START, on_voice)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)
    _cue_set(bench, 0x0100, cues=(6,))
    for playing, n, called, r3 in ((0, 50, True, 50), (0, 36, True, 0), (1, 36, False, None)):
        seen.clear()
        b.mu.mem_write(clip + 0x28, bytes([playing]))
        b.mu.mem_write(clip + 0x260, bytes([playing]))
        sp = bd.BENCH_SP - 0x20
        b.w32(sp, 0x61)
        b.w32(sp + 4, 0x62)
        for reg, v in ((A.UC_ARM_REG_R0, clip), (A.UC_ARM_REG_R1, 0x11), (A.UC_ARM_REG_R2, 0x12),
                       (A.UC_ARM_REG_R3, n), (A.UC_ARM_REG_SP, sp), (A.UC_ARM_REG_LR, bd.STOP | 1)):
            b.mu.reg_write(reg, v)
        b.mu.emu_start(sym["hook_voice"] | 1, bd.STOP, count=4000)
        tag = f"playing={playing} note={n}"
        res.check(b.mu.reg_read(A.UC_ARM_REG_PC) == bd.STOP, f"{tag}: returned")
        res.check(b.mu.reg_read(A.UC_ARM_REG_SP) == sp, f"{tag}: sp kept")
        res.check(bool(seen) == called, f"{tag}: voice start {'called' if called else 'skipped'}")
        if called and seen:
            v = seen[0]
            res.check(v["r0"] == clip and v["r1"] == 0x11 and v["r2"] == 0x12 and v["r3"] == r3,
                      f"{tag}: args {v}")
            res.check([v["a5"], v["a6"]] == [0x61, 0x62], f"{tag}: stack args")
            res.check(v["lr"] | 1 == bd.STOP | 1, f"{tag}: return address")
    b.unstub(VOICE_START)
    res.check(_cue(b, 4)["pending"] == 5, "swallowed note left the jump pending")


# ---- 3.1.E Clip editor: "..." row (Cue / Loop / Speed / Grid), Keys A-H, Beat Count from a file BPM
WAVE_VIEW_CTOR = 0x080A5B90
VIEW = 0xC1000000                # bench-only SDRAM scratch for a real waveform view
VIEW_PAIRS = 0xC1010000
VIEW_EV = 0xC1020000
UI_TICK = 0x2402EEE0
UI_POST = 0x080AED14
NOTE_ON = 0x0804C61C
NOTE_OFF = 0x0804C5D4
VIEW_UPDATE = 0x080A54A0
KNOB_TURN = 0x080B0D54
ROW_BTN = (0x20C8, 0x2028, 0x1EE8, 0x1F88)    # Split Join Scan Grid, left to right
PAD_DK = DK_BASE + 0x40 + 4 * 16              # DkPad of row 1 col 0
DK_UI = DK_BASE + 0x340
DK_MARKS = DK_BASE + 0x3A0
DK_GRIDPOS = DK_BASE + 0x3A8
DK_GRID = DK_BASE + 0x2C0      # DkGrid[16]: ofs, size
DK_BTN = DK_BASE + 0x4A8       # three stock buttons in SRAM4
ID_GRID = 0x1AB
ID_GRIDOFS = 0x1AC
POS_END = 0xFFFF


class WaveView:
    """A real stock waveform view (FUN_080A5B90) for row 1, col 0. UI posts are queued and applied after
    each call (a firmware call from inside a stub would nest the emulator)."""

    def __init__(self, bench, cellmode=1, ut=960, units=200, play=None):
        b = self.b = bench.b
        self.sym = bench.sym
        b.call(self.sym["deck_init"])
        b.mu.mem_write(VIEW, b"\0" * 0x6000)
        b.w32(0x2400008C, 0xC2000000)           # stock bump heap: a fresh bench window per view (each view
        b.w32(0x24000088, 0xC3000000)           # allocates its param lists, 96 rows each in 3.1.E)
        b.call(WAVE_VIEW_CTOR, VIEW)
        if b.u32(VIEW + 0x38 + 0x184) == 0:     # the waveform's 128-marker buffer (0x200 bytes on the device)
            b.mu.mem_write(0xC1050000, b"\0" * 0x200)
            b.w32(VIEW + 0x38 + 0x184, 0xC1050000)
        bag = self.bag = VIEW + 0x2370
        b.mu.mem_write(VIEW_PAIRS, b"\0" * 0x400)
        b.w32(bag + 4, VIEW_PAIRS)
        b.mu.mem_write(bag + 10, b"\x00\x00\x60\x00")
        b.call(PARAM_ADD, bag, 0x8B, cellmode)
        b.call(PARAM_ADD, bag, 0x8D, 1)
        b.call(self.sym["deck_bag_defaults"], bag, 0)
        b.mu.mem_write(VIEW + 0x29C, (0x10).to_bytes(2, "little"))
        b.mu.mem_write(PAD_DK + 8, struct.pack("<f", 48000.0 * ut / 960))
        b.mu.mem_write(PAD_DK + 12, ut.to_bytes(2, "little") + units.to_bytes(2, "little"))
        b.call(self.sym["deck_store_unpacked"], 0x0100, ID_GRID, 4)
        if play is not None:
            b.mu.mem_write(CUE_BASE + 4 * 24 + 22, play.to_bytes(2, "little"))
        self.posts, self.pend, self.notes, self.updates, self.turns = [], [], [], [], []
        b.stub(UI_POST, self._post)
        b.stub(NOTE_ON, lambda bb: self.notes.append(("on", bb.arg(1), bb.arg(2))))
        b.stub(NOTE_OFF, lambda bb: self.notes.append(("off", bb.arg(1), bb.arg(2))))
        b.stub(VIEW_UPDATE, lambda bb: self.updates.append(bb.arg(1)))

    def _post(self, bb):
        ev = bb.arg(1)
        id_, val = bb.u32(ev + 12), bb.u32(ev + 16)
        val = val - (1 << 32) if val >> 31 else val
        self.posts.append((id_, val))
        self.pend.append((id_, val))

    def flush(self):
        while self.pend:
            id_, val = self.pend.pop(0)
            self.b.call(PARAM_SET, self.bag, id_, val & 0xFFFFFFFF)
            self.b.call(self.sym["deck_store_unpacked"], 0x0100, id_, val & 0xFFFFFFFF)

    def get(self, id_):
        v = self.b.call(PARAM_GET, self.bag, id_)
        return v - (1 << 32) if v >> 31 else v

    def set(self, id_, val):
        self.b.call(PARAM_SET, self.bag, id_, val & 0xFFFFFFFF)
        self.b.call(self.sym["deck_store_unpacked"], 0x0100, id_, val & 0xFFFFFFFF)

    def bar(self, mode=1, is_slicer=0):
        self.b.call(self.sym["deck_bar"], VIEW, is_slicer, mode)

    def update(self):
        """What stock's next screen update does for a Clip: bar, then the Clip case (titles, lights)."""
        self.bar()
        lst = 0xC1030000
        self.b.w32(lst, 0)
        self.b.w32(lst + 4, 0)
        self.b.call(self.sym["deck_ui_update"], VIEW + 0x38, lst, VIEW)

    def event(self, typ, id_, tick=None):
        b = self.b
        if tick is not None:
            b.w32(UI_TICK, tick)
        b.mu.mem_write(VIEW_EV, b"\0" * 0x20)
        b.mu.mem_write(VIEW_EV, typ.to_bytes(2, "little"))
        b.w32(VIEW_EV + 12, id_)
        r = b.call(self.sym["deck_ui_event"], VIEW, VIEW_EV)
        self.flush()
        return r

    def tap(self, id_):
        self.event(1, id_)
        return self.event(2, id_)

    def hold_cue(self, on):
        self.b.mu.mem_write(self.btn(0) + 0x98, bytes([1 if on else 0]))

    def knob(self, off, delta):
        self.b.stub(KNOB_TURN, lambda bb: self.turns.append((bb.arg(0) - VIEW, bb.arg(1))))
        self.b.call(self.sym["deck_knob_turn"], VIEW + off, delta & 0xFFFFFFFF, VIEW)
        self.b.unstub(KNOB_TURN)
        self.flush()

    def btn(self, i):
        if i < 4:
            return VIEW + ROW_BTN[i]
        if i < 9:
            return VIEW + 0x2F8 + 0x3C + (i - 4) * 0xA0
        return DK_BTN + (i - 9) * 0xA0

    def bid(self, i):
        return (0x45, 0x46, 0x47, 0x48)[i] if i < 4 else 0x1C0 + i - 4

    def labels(self, idx):
        return [self.b.cstr(self.btn(i) + 0x65, 16) for i in idx]

    def hidden(self, idx):
        return [self.b.u8(self.btn(i) + 0x30) for i in idx]

    def lit(self, idx):
        return [self.b.u8(self.btn(i) + 0x96) for i in idx]

    def rect(self, i):
        return [self.b.u32(self.btn(i) + 4 * k) - (1 << 32) if self.b.u32(self.btn(i) + 4 * k) >> 31
                else self.b.u32(self.btn(i) + 4 * k) for k in range(1, 5)]

    def titles(self):
        """The titles as shown: the knob's display label (+0xF4, text at +0x31), not the source text
        at +0x75 (device report: the shown titles lagged one layer behind)."""
        return [self.b.cstr(VIEW + o + 0xF4 + 0x31, 24) for o in (0x670, 0xC30, 0x950, 0xF10)]

    def marker(self, beat, sample_rate=48000):
        """Put the waveform's white centre marker on a beat (Sync 1/4: one unit per beat)."""
        wave = VIEW + 0x38
        self.b.mu.mem_write(wave + 0x168, struct.pack("<f", 600.0))
        self.b.w32(wave + 0x23C, 159)
        self.b.w32(wave + 0x240, 159)
        self.b.w32(wave + 0x244, int(beat * sample_rate))


TL, TR, BL, BR = 0x670, 0xC30, 0x950, 0xF10
POP = (3, 4, 5, 6, 7, 8, 9, 10)          # popup items, left to right
FRAME = 11


def _app_cell(bench, mode):
    """The app's own cell for row 1, col 0 (what stock's waveform update copies into the view)."""
    b = bench.b
    cell = b.call(0x08098D0C, 0x24020088, VIEW + 0x298)
    if b.u32(cell + 4) == 0:
        b.w32(cell + 4, 0xC1080000)
        b.mu.mem_write(0xC1080000, b"\0" * 0x800)
        b.mu.mem_write(cell + 10, b"\x00\x00\xff\x00")
        b.call(PARAM_ADD, cell, 0x8B, mode)
        b.call(bench.sym["deck_bag_defaults"], cell, 0)
    else:
        b.call(PARAM_SET, cell, 0x8B, mode)
    return cell


def t_ui_dots_visible(bench, res):
    """Device report: "..." was a black box. Stock's update hides it unless the pad is a Slicer
    (0x080A54F6: view+0x1D18 = mode != 2); deck_bar shows it again on Clip. Runs stock's whole update,
    then presses "..." through the real (hooked) handler."""
    b = bench.b
    for mode, want_hidden, bar in ((1, 0, "slicer"), (0, 1, "plain"), (2, 0, "slicer")):
        v = WaveView(bench, cellmode=mode)
        b.unstub(VIEW_UPDATE)
        _app_cell(bench, mode)
        b.call(VIEW_UPDATE, VIEW, 1)
        b.call(VIEW_UPDATE, VIEW, 0)
        hidden = b.u8(VIEW + 0x1CE8 + 0x30)
        shown = "slicer" if not b.u8(VIEW + 0x1568 + 0x30) else "plain"
        res.check(hidden == want_hidden and shown == bar, f"mode {mode}: ... hidden {hidden}, {shown} bar")
        if mode == 1:
            b.mu.mem_write(VIEW_EV, b"\0" * 0x20)
            b.mu.mem_write(VIEW_EV, (2).to_bytes(2, "little"))
            b.w32(VIEW_EV + 12, 0x4C)
            b.call(0x080A66DC, VIEW, VIEW_EV)
            res.check(b.u8(VIEW + 0x2F8 + 0x30) == 0 and b.u8(VIEW + 0x2168 + 0x30) == 1,
                      "... opens the row in place of the strip")
            res.check(v.labels((0, 1, 2)) == ["Cue", "Loop", "Grid"] and v.hidden(range(12)) == [0, 0, 0] + [1] * 9,
                      f"row shows {v.labels((0, 1, 2))}, popup hidden")
            res.check(v.titles() == ["Str 1.1.1", "End end", "Grid 1 bar", "Zoom 2 beats"], f"main titles shown {v.titles()}")
            v.tap(0x45)
            res.check(v.titles()[0] == "Cue A" and v.titles()[3] == "Swap", f"Cue titles shown at once {v.titles()}")


def t_ui_row_tabs(bench, res):
    v = WaveView(bench)
    v.bar(mode=1)
    b = bench.b
    res.check(v.labels((0, 1, 2)) == ["Cue", "Loop", "Grid"], f"Clip tabs {v.labels((0, 1, 2))}")
    res.check([v.rect(i)[0] for i in range(3)] == [0, 106, 213], f"three equal tabs {[v.rect(i)[0] for i in range(3)]}")
    res.check(b.u32(VIEW + 0x2F8 + 0x38) & 0xFFFF == 5, "five row spares added, never a sixth")
    res.check(b.u32(DK_UI + 0x38) == VIEW and b.u32(DK_BTN + 0x14) == 0x1C5, "SRAM4 buttons built for this view")
    v.bar(mode=1)
    res.check(b.u32(VIEW + 0x2F8 + 0x38) & 0xFFFF == 5, "built once")
    v.bar(mode=2, is_slicer=1)
    res.check(v.labels(range(4)) == ["Split", "Join", "Scan", "Grid"], f"Slicer row {v.labels(range(4))}")
    res.check([v.rect(i)[0] for i in range(4)] == [0, 80, 160, 240] and v.hidden(range(12)) == [0] * 4 + [1] * 8,
              "Slicer layout as stock")
    v2 = WaveView(bench, cellmode=2)
    v2.bar(mode=2, is_slicer=1)
    res.check(v2.tap(0x48) == 0 and v2.tap(0x45) == 0, "Slicer: Grid / Split stay stock events")


def t_ui_popups(bench, res):
    v = WaveView(bench)
    v.bar()
    v.tap(0x45)
    res.check(v.rect(FRAME) == [20, 50, 280, 40] and not v.hidden([FRAME])[0], f"popup box at Scan's place {v.rect(FRAME)}")
    res.check(v.labels(POP) == list("ABCDEFGH") and v.hidden(POP) == [0] * 8, f"Cue: A-H {v.labels(POP)}")
    xs = [v.rect(i) for i in POP]
    res.check(all(24 <= r[0] and r[0] + r[2] <= 296 and r[1] == 54 and r[3] == 32 for r in xs) and
              all(xs[i + 1][0] > xs[i][0] + xs[i][2] for i in range(7)), f"letters inside the box {xs[:2]}")
    res.check(v.lit((0, 1, 2)) == [1, 0, 0], "Cue tab lit")
    v.tap(0x46)
    res.check(v.labels(POP[:3]) == ["On", "Instant", "Nudge"] and v.hidden(POP) == [0, 0, 0] + [1] * 5, "Loop popup")
    v.tap(0x47)
    res.check(v.labels(POP[:4]) == ["Lines", "< 1", "1 >", "Raw"] and v.hidden(POP) == [0] * 4 + [1] * 4, "Grid popup")
    v.tap(0x47)
    res.check(v.hidden(POP + (FRAME,)) == [1] * 9, "same tab again closes the popup")
    v.tap(0x46)
    res.check(v.event(2, 0x4C) == 1 and v.hidden(POP + (FRAME,)) == [1] * 9, "... closes the popup, row stays")
    res.check(v.event(2, 0x4C) == 0, "... again: stock closes the row")
    # a tap in the box reaches the popup button (row is the head of the view's list), once "..." opened it
    v.tap(0x45)
    bench.b.call(0x080C4E60, VIEW + 0x2F8, 0)
    pt, out = 0x2001CE20, 0x2001CE30
    r = v.rect(POP[2])
    bench.b.w32(pt, r[0] + 3)
    bench.b.w32(pt + 4, r[1] + 3)
    bench.b.w32(out, 0)
    bench.b.call(0x080AEB5E, VIEW, pt, out, 0)
    res.check(bench.b.u32(out) == v.btn(POP[2]), "a touch on C hits C")
    bench.b.w32(pt, 22)
    bench.b.w32(pt + 4, 52)
    bench.b.w32(out, 0)
    bench.b.call(0x080AEB5E, VIEW, pt, out, 0)
    res.check(bench.b.u32(out) == v.btn(FRAME), "a touch between letters hits the box, not the knob behind")


def t_ui_cue_letters(bench, res):
    v = WaveView(bench)
    v.bar()
    v.tap(0x45)
    a, b_, c = v.bid(POP[0]), v.bid(POP[1]), v.bid(POP[2])
    res.check(v.lit(POP)[:2] == [1, 0], "Cue A is set by default (at the start): lit")
    v.marker(9.4)                            # white marker just after beat 9
    v.posts.clear()
    v.tap(b_)
    res.check(v.posts == [(ID_CUE1 + 1, 10), (ID_CUEMODE, 1)], f"dark B: cue at the white marker, beat 9 {v.posts}")
    v.marker(9.6)
    v.posts.clear()
    v.tap(c)
    res.check(v.posts[0] == (ID_CUE1 + 2, 11), f"marker past half a beat rounds to the next {v.posts}")
    v.bar()
    res.check(v.lit(POP)[:3] == [1, 1, 1], "A, B, C lit")
    v.notes.clear()
    v.event(1, b_)
    res.check(v.notes == [("on", 0x0100, 37)], f"lit B down: note on {v.notes}")
    v.event(2, b_)
    res.check(v.notes[-1] == ("off", 0x0100, 37), "lit B up: note off")
    v.posts.clear()
    v.marker(20)
    v.hold_cue(True)
    v.event(1, 0x45)
    v.event(1, b_)
    v.event(2, b_)
    res.check(v.posts == [(ID_CUE1 + 1, 0)], f"hold Cue + lit B: cleared in one tap {v.posts}")
    res.check(v.lit(POP)[1] == 0, "B dark after the clear")
    v.event(1, b_)
    v.event(2, b_)
    res.check(v.posts[-1] == (ID_CUE1 + 1, 21), f"hold Cue + dark B: set at the marker (overwrite) {v.posts}")
    v.hold_cue(False)
    v.posts.clear()
    v.event(2, 0x45)
    res.check(v.hidden(POP) == [0] * 8 and v.posts == [], "Cue release after a hold edit: popup stays")
    # one-finger clear: the position knob turned left past the start
    v.set(ID_CUE1 + 3, 3)                    # Cue D at beat 2
    bench.b.mu.mem_write(DK_UI + 1, b"\x03")  # D selected
    v.posts.clear()
    v.knob(TR, -400)
    res.check(v.posts == [(ID_CUE1 + 3, 0)], f"Pos turned past the start (Grid 1 bar) clears D {v.posts}")
    v.update()
    res.check(v.titles()[1] == "Pos Off", f"title {v.titles()}")
    v.set(ID_CUE1 + 3, 9)
    v.posts.clear()
    v.knob(TR, 400)
    res.check(v.posts == [(ID_CUE1 + 3, 13)], f"Pos + one Grid step = 4 beats {v.posts}")


def t_ui_cue_flags(bench, res):
    """Set cues show as marker flags on the waveform, the selected one highlighted (wave+0x25C)."""
    v = WaveView(bench, units=64)
    b = bench.b
    for k, beat in ((0, 4), (2, 12), (5, 40)):
        v.set(ID_CUE1 + k, beat + 1)
    v.bar()
    v.tap(0x45)
    v.knob(TL, 800)                           # select C
    v.update()
    n = b.u32(DK_MARKS + 4)
    res.check(n > 3, f"grid lines on by default, after the flags ({n} markers)")
    b.mu.mem_write(DK_UI + 3, b"\x00")         # Lines off: the flags alone
    v.update()
    n = b.u32(DK_MARKS + 4)
    flags = [b.u32(DK_GRIDPOS + 4 * i) for i in range(n)]
    res.check(flags == [4 * 48000, 12 * 48000, 40 * 48000], f"three flags {flags}")
    res.check(b.u32(VIEW + 0x38 + 0x188) == 3 and b.u32(VIEW + 0x38 + 0x25C) == 2, "handed to the waveform, C highlighted")
    v.set(ID_GRIDOFS, 480)
    v.update()
    res.check(b.u32(DK_GRIDPOS) == 4 * 48000 + 480, "flags follow the grid offset")


def t_titles_every_update(bench, res):
    """Device: Grid looked stuck. A param change (0x66) runs stock's update with force 0, which skips the
    Clip case; deck_bar (always called) now refreshes the titles."""
    v = WaveView(bench)
    v.bar()
    v.set(ID_GRID, 6)
    v.bar()
    res.check(v.titles()[2] == "Grid 4 bars", f"bar alone refreshes {v.titles()}")
    w = WaveView(bench)
    bench.b.call(PARAM_SET, w.bag, ID_GRID, 0)       # stand-in for a bag with no Grid row is below
    bag = VIEW + 0x2370
    rows = bench.b.u32(bag + 4)
    n = bench.b.u32(bag + 8) >> 16
    kept = []
    for k in range(n):
        if int.from_bytes(bytes(bench.b.mu.mem_read(rows + 8 * k, 2)), "little") != ID_GRID:
            kept.append(bytes(bench.b.mu.mem_read(rows + 8 * k, 8)))
    bench.b.mu.mem_write(rows, b"".join(kept) + b"\0" * 8)
    bench.b.mu.mem_write(bag + 10, len(kept).to_bytes(2, "little"))
    w.bar()
    res.check(w.titles()[2] == "Grid 1 bar", f"no Grid row (older preset): 1 bar {w.titles()}")


def t_keys_lit(bench, res):
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    key = 0x2001CE80
    b.mu.mem_write(key, b"\0" * 0x40)
    _cue_set(bench, 0x0100, cues=(1, 0, 9))
    b.w32(DK_UI + 0x20, 0x10)
    b.mu.mem_write(DK_UI + 0x24, b"\x01")
    got = [(b.call(sym["deck_key_lit"], 36 + k, key), b.u8(key + 0x35)) for k in range(4)]
    res.check(got == [(1, 1), (0, 0), (1, 1), (0, 0)], f"set cues lit {got}")
    b.mu.mem_write(DK_UI + 0x24, b"\x00")
    seen = []
    b.stub(0x080BA3FC, lambda bb: (seen.append(bb.arg(0)), 7)[1])
    r = b.call(sym["deck_key_lit"], 60, key)
    b.unstub(0x080BA3FC)
    res.check(r == 7 and seen == [60], "not cue keys: stock scale check")


def t_list_value(bench, res):
    """Info list rows for the cues print bar.beat.16th, '-' when unset (hook at 0x080A9218)."""
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    buf, row, fmt = 0x2001CE40, 0x2001CE80, b.put_cstr(0x2001CF00, "%d")
    for pid, val, want in ((ID_CUE1, 0, "Off"), (ID_CUE1 + 7, 1, "1.1"), (ID_CUE1, 37, "10.1"),
                           (ID_DECK + 1, POS_END, "end"), (ID_DECK, 16, "2.1.1"), (0x4E, 37, "37"),
                           (0xD2, 0, "Auto"), (0xD2, 1024, "1024")):
        b.mu.mem_write(row + 0x94, pid.to_bytes(2, "little"))
        b.call(sym["deck_list_value"], buf, 0x1F, fmt, val, row)
        res.check(b.cstr(buf) == want, f"{pid:#x} = {val}: {b.cstr(buf)!r} (want {want!r})")
    A = bd.A
    b.mu.mem_write(row + 0x94, (ID_CUE1).to_bytes(2, "little"))
    for reg, val in ((A.UC_ARM_REG_R0, buf), (A.UC_ARM_REG_R1, 0x1F), (A.UC_ARM_REG_R2, fmt), (A.UC_ARM_REG_R3, 17),
                     (A.UC_ARM_REG_R4, row), (A.UC_ARM_REG_SP, bd.BENCH_SP), (A.UC_ARM_REG_LR, bd.STOP | 1)):
        b.mu.reg_write(reg, val)
    b.mu.emu_start(sym["hook_list_value"] | 1, bd.STOP, count=20000)
    res.check(b.cstr(buf) == "5.1" and b.mu.reg_read(A.UC_ARM_REG_R4) == row and
              b.mu.reg_read(A.UC_ARM_REG_SP) == bd.BENCH_SP, "stub passes the row from r4")


def t_ui_main_knobs(bench, res):
    v = WaveView(bench)
    v.bar()
    v.knob(TL, 400)
    res.check(v.posts == [(ID_DECK, 16)] and v.turns == [], f"Start + 1 bar (no stock turn) {v.posts}")
    v.update()
    res.check(v.titles()[0] == "Str 2.1.1" and v.titles()[1] == "End end", f"titles {v.titles()}")
    v.posts.clear()
    v.knob(TR, -400)
    res.check(v.posts == [(ID_DECK + 1, 784)], f"End - 1 bar from the end (800/16) {v.posts}")
    v.knob(TR, 400)
    res.check(v.posts[-1] == (ID_DECK + 1, POS_END), "back to the end: stored as end of file")
    v.posts.clear()
    v.knob(BL, 400)
    res.check(v.posts == [(ID_GRID, 5)], f"Grid 1 bar -> 2 bars {v.posts}")
    wave = VIEW + 0x38
    v.marker(20)                              # 600 samples per pixel
    v.knob(BR, 800)
    z = struct.unpack("<f", bench.b.mu.mem_read(wave + 0x24C, 4))[0]
    res.check(v.posts == [(ID_GRID, 5)] and abs(z - 300.0) < 0.5, f"bottom right: zoom in x2 -> {z} spp, no post")
    bench.b.mu.mem_write(wave + 0x168, struct.pack("<f", z))   # the widget's tick applies it
    bench.b.w32(wave + 0x24C, 0)
    v.update()
    res.check(v.titles()[3] == "Zoom 2 beats", f"zoom title: the screen's width ({v.titles()[3]})")
    v.knob(BR, -400 * 30)
    z = struct.unpack("<f", bench.b.mu.mem_read(wave + 0x24C, 4))[0]
    res.check(z == 4096.0, f"zoom out stops at 4096 spp ({z})")
    v.posts.clear()
    v.knob(BL, -4000)
    res.check(v.posts == [(ID_GRID, 2)], f"Grid down to the Sync unit (1/4) {v.posts}")
    v.knob(TL, 399)
    res.check(len(v.posts) == 1, "399 counts: no step")
    w = WaveView(bench, ut=3840, units=50)    # Sync 1 bar
    w.knob(BL, -1600)
    res.check(w.posts == [(ID_GRID, 4)], f"Sync 1 bar: Grid stays >= 1 bar {w.posts}")
    v3 = WaveView(bench, cellmode=0)
    v3.knob(TL, 50)
    res.check(v3.turns == [(TL, 50)], "Sample pad: stock turn")


def t_ui_loop_popup(bench, res):
    v = WaveView(bench, play=20, units=64)
    v.marker(20)
    v.bar()
    v.set(ID_DECK + 2, 32)
    v.set(ID_DECK + 3, 64)                   # loop bar 3 to bar 5
    v.tap(0x46)
    v.update()
    res.check(v.titles() == ["LpS 3.1.1", "LpE 5.1.1", "Size 2 bars", "Move"], f"loop titles {v.titles()}")
    v.posts.clear()
    v.knob(TL, 400)
    res.check(v.posts == [(ID_DECK + 2, 48)], f"Loop Start + 1 bar {v.posts}")
    v.posts.clear()
    v.knob(TR, 400)
    res.check(v.posts == [(ID_DECK + 3, 80)], f"Loop End + 1 bar {v.posts}")
    v.posts.clear()
    v.knob(BL, -400)
    res.check(v.posts == [(ID_DECK + 3, 64)], f"size /2: 48..80 -> 48..64 {v.posts}")
    v.posts.clear()
    v.knob(BL, 800)
    res.check(v.posts == [(ID_DECK + 3, 112)], f"size x4: 48..112 {v.posts}")
    v.posts.clear()
    v.knob(BR, -400)
    res.check(v.posts == [(ID_DECK + 2, 32), (ID_DECK + 3, 96)], f"move - 1 bar {v.posts}")
    v.posts.clear()
    v.tap(v.bid(POP[2]))
    res.check(v.posts == [(ID_DECK + 2, 48), (ID_DECK + 3, 112)], f"Nudge + 1 grid step {v.posts}")
    v.posts.clear()
    v.tap(v.bid(POP[1]))
    res.check(v.posts == [(ID_DECK + 2, 80), (ID_DECK + 3, 144)], f"Instant at the marker (bar 6) {v.posts}")
    for beat, want in ((21, 80), (23, 96)):
        v.marker(beat)
        v.posts.clear()
        v.tap(v.bid(POP[1]))
        res.check(v.posts[:1] == [(ID_DECK + 2, want)], f"Instant, marker on beat {beat}: nearest bar {v.posts}")
    v.marker(20)
    v.tap(v.bid(POP[1]))
    v.posts.clear()
    v.tap(v.bid(POP[0]))
    v.bar()
    res.check(v.posts == [(0x8D, 0)] and v.lit(POP[:1]) == [0], f"On toggles Loop Mode {v.posts}")
    # halving keeps the half the playhead is in: loop 80..144, playhead beat 30 (1/16 120) in the 2nd half
    bench.b.mu.mem_write(CUE_BASE + 4 * 24 + 22, (30).to_bytes(2, "little"))
    v.posts.clear()
    v.knob(BL, -400)
    res.check(v.posts == [(ID_DECK + 2, 112), (ID_DECK + 3, 144)], f"halve: playhead stays inside {v.posts}")


def t_ui_grid_popup(bench, res):
    v = WaveView(bench)
    v.bar()
    v.tap(0x47)
    v.update()
    res.check(v.titles() == ["Nudge +0ms", "", "Grid 1 bar", "Zoom 2 beats"], f"grid titles {v.titles()}")
    v.posts.clear()
    v.knob(TL, 800)
    res.check(v.posts == [(ID_GRIDOFS, 96)], f"nudge 2 ms {v.posts}")
    v.posts.clear()
    v.tap(v.bid(POP[2]))
    res.check(v.posts == [(ID_GRIDOFS, 96 + 192000)], f"1 >: one bar of samples {v.posts}")
    v.tap(v.bid(POP[1]))
    v.tap(v.bid(POP[1]))
    res.check(v.posts[-1] == (ID_GRIDOFS, 96 - 192000), f"< 1 twice {v.posts}")
    res.check(bench.b.u8(DK_UI + 3) == 1, "Lines on by default")
    v.tap(v.bid(POP[0]))
    res.check(bench.b.u8(DK_UI + 3) == 0 and v.updates == [1], "Lines off, view redrawn")
    v.tap(v.bid(POP[3]))
    v.bar()
    res.check(bench.b.u8(DK_UI + 2) == 1 and v.lit(POP[:4]) == [0, 0, 0, 1], "Raw lit")
    v.posts.clear()
    v.knob(TL, 50)
    res.check(v.posts == [(ID_GRIDOFS, 96 - 192000 + 48)], f"Raw: 50 counts x 8 = one step {v.posts}")


def _grid(bench, ut, units, spp, anchor=0, anchor_px=0, grid=4, ofs=0):
    v = WaveView(bench, ut=ut, units=units)
    b = bench.b
    v.set(ID_GRID, grid)
    v.set(ID_GRIDOFS, ofs)
    b.mu.mem_write(DK_UI + 3, b"\x01")
    wave = VIEW + 0x38
    b.w32(wave + 4, 0)
    b.w32(wave + 0xC, 320)
    b.mu.mem_write(wave + 0x168, struct.pack("<f", spp))
    b.w32(wave + 0x240, anchor_px)
    b.w32(wave + 0x244, anchor)
    lst = 0xC1030000
    b.w32(lst, 0)
    b.w32(lst + 4, 0)
    b.call(bench.sym["deck_ui_update"], wave, lst, VIEW)
    n = b.u32(DK_MARKS + 4)
    return [b.u32(DK_GRIDPOS + 4 * i) for i in range(n)], b.u32(wave + 0x188), b.u8(wave + 0x238)


def t_ui_grid_marks(bench, res):
    bar = 48000 * 4
    pos, shown, flag = _grid(bench, 960, 640, spp=600.0, anchor=48000 * 300)   # zoomed into beat ~300
    steps = {pos[i + 1] - pos[i] for i in range(len(pos) - 1)}
    res.check(0 < len(pos) <= 64 and steps == {48000}, f"Grid 1 bar: a line per beat ({len(pos)}, {steps})")
    res.check(shown == len(pos) and flag == 1, f"handed to the waveform ({shown}, shown {flag})")
    pos, _, _ = _grid(bench, 960, 640, spp=48000 * 640 / 320)               # whole file on screen
    steps = {pos[i + 1] - pos[i] for i in range(len(pos) - 1)}
    res.check(0 < len(pos) <= 64 and all(s % bar == 0 and s / (48000 * 640 / 320) >= 10 for s in steps),
              f"zoomed out: beats thinned to whole bars, >= 10 px ({sorted(steps)[:2]})")
    pos, _, _ = _grid(bench, 960, 640, spp=600.0, anchor=48000 * 300, grid=2)
    res.check({pos[i + 1] - pos[i] for i in range(len(pos) - 1)} == {48000}, "Grid 1/4: a line per beat")
    pos, _, _ = _grid(bench, 960, 640, spp=600.0, anchor=48000 * 300, ofs=480)
    res.check(pos and all(p % 48000 == 480 for p in pos), "offset moves the lines")


def t_grain_offset(bench, res):
    """The grid offset moves every grain: deck_grain_start and the hook_grain_ofs stub at 0x080656E2."""
    b = bench.b
    sym = bench.sym
    A = bd.A
    b.call(sym["deck_init"])
    clip = _deck_clip(b, units=16)
    b.call(sym["deck_rate"], clip)
    res.check(b.call(sym["deck_grain_start"], clip, 96000) == 96000, "no offset: unchanged")
    b.call(sym["deck_store_unpacked"], 0x0100, ID_GRIDOFS, 480)
    res.check(b.call(sym["deck_grain_start"], clip, 96000) == 96480, "offset 480")
    b.call(sym["deck_store_unpacked"], 0x0100, ID_GRIDOFS, (-480) & 0xFFFFFFFF)
    res.check(b.call(sym["deck_grain_start"], clip, 0) == 0, "clamped at the start")
    b.call(sym["deck_store_unpacked"], 0x0100, ID_GRIDOFS, 480)
    res.check(b.call(sym["deck_grain_start"], clip, 32768 * 16 - 1) == 32768 * 16 - 1, "clamped at the end")
    b.mu.mem_write(clip + 0x254, b"\x01")
    res.check(b.call(sym["deck_grain_start"], clip, 1000) == 1000, "Slicer untouched")
    b.mu.mem_write(clip + 0x254, b"\x00")
    for reg, val in ((A.UC_ARM_REG_R0, 0xA0), (A.UC_ARM_REG_R1, 0xA1), (A.UC_ARM_REG_R2, 0xA2), (A.UC_ARM_REG_R3, 0xA3),
                     (A.UC_ARM_REG_R4, clip), (A.UC_ARM_REG_R11, 0), (A.UC_ARM_REG_SP, bd.BENCH_SP),
                     (A.UC_ARM_REG_LR, bd.STOP | 1)):
        b.mu.reg_write(reg, val)
    for i in range(16):
        b.mu.reg_write(A.UC_ARM_REG_S0 + i, 0x40000000 + i)
    b.mu.reg_write(A.UC_ARM_REG_S15, 5000)
    b.mu.emu_start(sym["hook_grain_ofs"] | 1, bd.STOP, count=20000)
    regs = [b.mu.reg_read(r) for r in (A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3,
                                       A.UC_ARM_REG_R11, A.UC_ARM_REG_LR, A.UC_ARM_REG_SP)]
    vfp = [b.mu.reg_read(A.UC_ARM_REG_S0 + i) for i in range(15)]
    res.check(regs == [0xA0, 0xA1, 0xA2, 0xA3, 5480, 1, bd.BENCH_SP], f"stub: fp = 5480, lr = 1, regs kept {[hex(r) for r in regs]}")
    res.check(vfp == [0x40000000 + i for i in range(15)] and b.mu.reg_read(A.UC_ARM_REG_PC) == bd.STOP,
              "stub keeps s0-s14 and returns")
    # cue prefetch asks for the offset position too
    seen = []
    b.stub(0x0806375C, lambda bb: seen.append(bb.arg(1)))
    ref = 0x2001CC00
    b.mu.mem_write(ref, b"\0" * 0x40)
    b.w32(clip + 0x24, ref)
    b.w32(ref + 0x20, 0xC1070000)
    b.mu.mem_write(0xC1070000 + 0x4FF8, b"\0" * 8)
    _cue_set(bench, 0x0100, cues=(6,))
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.call(sym["deck_cue_note"], clip, 36)
    b.unstub(0x0806375C)
    res.check(seen == [5 * 32768 + 480 - 8], f"prefetch at unit 5 + offset {seen}")


def t_keys_hooks(bench, res):
    b = bench.b
    sym = bench.sym
    b.call(sym["deck_init"])
    buf = 0x2001CE40
    fmt = b.put_cstr(0x2001CE80, "%d")
    b.call(sym["deck_keys_label"], buf, 16, fmt, 3)
    res.check(b.cstr(buf) == "3", "keys off: stock number")
    b.mu.mem_write(DK_UI + 0x24, b"\x01")
    b.call(sym["deck_keys_label"], buf, 16, fmt, 3)
    res.check(b.cstr(buf) == "C", "cue keys: letter")
    b.call(sym["deck_keys_label"], buf, 16, fmt, 9)
    res.check(b.cstr(buf) == "9", "past H: number")
    b.w32(DK_UI + 0x20, 0x10)
    _cue_set(bench, 0x0100, note=60)
    res.check(b.call(sym["deck_keys_note"], 0x0100, 36) == 60 and b.call(sym["deck_keys_note"], 0x0100, 43) == 67,
              "keys 36..43 -> Cue Note 60..67")
    res.check(b.call(sym["deck_keys_note"], 0x0100, 35) == CUE_SWALLOW, "STOP dropped")
    res.check(b.call(sym["deck_keys_note"], 0x0101, 36) == 36, "other pad untouched")
    res.check(b.call(sym["deck_keys_note"], 0x0100, 44) == 44, "past the 8 untouched")
    # refresh: Clip in Cue Mode forces the Slicer layout
    kv = 0xC1040000
    b.mu.mem_write(kv, b"\0" * 0x4600)
    b.mu.mem_write(kv + 0x1C30, (0x10).to_bytes(2, "little"))
    seen = []
    b.stub(0x08099504, lambda bb: (bb.w32(bb.arg(3), 1), 1)[1])
    b.stub(0x080AD978, lambda bb: seen.append(bb.u8(bb.arg(0) + 0x4550)))
    b.call(sym["deck_keys_refresh"], kv)
    res.check(seen == [1] and b.u32(DK_UI + 0x20) == 0x10 and b.u8(DK_UI + 0x24) == 1, f"Clip + Cue Mode: cue keys {seen}")
    _cue_set(bench, 0x0100, mode=0)
    b.call(sym["deck_keys_refresh"], kv)
    res.check(b.u8(DK_UI + 0x24) == 1, "Cue Mode off but a cue set: cue keys at once")
    _cue_set(bench, 0x0100, mode=0, cues=())
    b.call(sym["deck_keys_refresh"], kv)
    res.check(b.u8(DK_UI + 0x24) == 0 and b.u32(DK_UI + 0x20) == 0xFFFF, "no cues, Cue Mode off: stock")
    b.unstub(0x08099504)
    b.unstub(0x080AD978)
    # count stub: r2 forced to 8 only with cue keys
    A = bd.A
    for on, want in ((1, 8), (0, 21)):
        b.mu.mem_write(DK_UI + 0x24, bytes([on]))
        base = 0xC1050000
        for reg, val in ((A.UC_ARM_REG_R2, 21), (A.UC_ARM_REG_R3, base), (A.UC_ARM_REG_SP, bd.BENCH_SP),
                         (A.UC_ARM_REG_LR, bd.STOP | 1)):
            b.mu.reg_write(reg, val)
        b.mu.emu_start(sym["hook_keys_count"] | 1, bd.STOP, count=200)
        res.check(b.u32(base + 0xAA4) == want, f"key count {b.u32(base + 0xAA4)} (cue keys {on})")


def t_keys_note_stub(bench, res):
    """hook_keys_on: (engine, key, note, 0, [sp] flag) reaches FUN_0804C61C with r3 / [sp] / lr / sp kept."""
    b = bench.b
    sym = bench.sym
    A = bd.A
    b.call(sym["deck_init"])
    _cue_set(bench, 0x0100, note=50)
    b.mu.mem_write(DK_UI + 0x24, b"\x01")
    b.w32(DK_UI + 0x20, 0x10)
    seen = []

    def on(bb):
        seen.append([bb.arg(i) for i in range(5)] + [bb.mu.reg_read(A.UC_ARM_REG_LR)])
        return 0

    b.stub(NOTE_ON, on)
    for note, want in ((37, 51), (35, None), (60, 60)):
        seen.clear()
        sp = bd.BENCH_SP - 0x20
        b.w32(sp, 0x77)
        for reg, val in ((A.UC_ARM_REG_R0, 0x2400A9C0), (A.UC_ARM_REG_R1, 0x0100), (A.UC_ARM_REG_R2, note),
                         (A.UC_ARM_REG_R3, 0), (A.UC_ARM_REG_SP, sp), (A.UC_ARM_REG_LR, bd.STOP | 1)):
            b.mu.reg_write(reg, val)
        b.mu.emu_start(sym["hook_keys_on"] | 1, bd.STOP, count=2000)
        ok = b.mu.reg_read(A.UC_ARM_REG_SP) == sp and b.mu.reg_read(A.UC_ARM_REG_PC) == bd.STOP
        if want is None:
            res.check(not seen and ok, f"note {note}: dropped, returns")
        else:
            res.check(seen and seen[0][:5] == [0x2400A9C0, 0x0100, want, 0, 0x77] and seen[0][5] | 1 == bd.STOP | 1 and ok,
                      f"note {note} -> {want}: args {seen}")
    b.unstub(NOTE_ON)


def t_bpm_names(bench, res):
    b = bench.b
    f = bench.sym["deck_name_bpm"]
    cases = [("Track 128bpm.wav", 12800), ("Loop_174.5_BPM.wav", 17450), ("BPM 90 drums.wav", 9000),
             ("bpm128.wav", 12800), ("track-140-bpm.wav", 14000), ("0:/Samples/120bpm/kick.wav", 0),
             ("0:/Samples/120bpm/Song 96 BPM.wav", 9600), ("no tempo.wav", 0), ("500bpm.wav", 0),
             ("Song 38 bpm.wav", 0), ("x 300.5bpm", 0), ("x 300bpm", 30000), ("bpm.wav", 0)]
    for name, want in cases:
        got = b.call(f, b.put_cstr(0x2001CE00, name))
        res.check(got == want, f"{name!r}: {got} (want {want})")


def t_bpm_auto_beats(bench, res):
    b = bench.b
    sym = bench.sym
    A = bd.A
    b.call(sym["deck_init"])
    clip = _deck_clip(b)
    ref, mgr, slots = 0x2001CC00, 0xC1070000, 0xC1060000
    b.mu.mem_write(ref, b"\0" * 0x40)
    b.w32(clip + 0x24, ref)
    b.w32(ref + 0x20, mgr)
    b.w32(ref + 0x24, 3)
    b.w32(mgr + 0x4348, slots)
    b.put_cstr(slots + 3 * 0x170 + 0x5C, "0:/Stems/Bass 128bpm.wav")
    b.stub(0x080636F0, lambda bb: 48000 * 300)        # 5:00
    b.stub(0x0806377C, lambda bb: 48000)
    res.check(b.call(sym["deck_auto_beats"], clip) == 640, "5:00 at 128 BPM = 640 beats")
    b.w32(clip + 0xB80, 16)
    res.check(b.call(sym["deck_auto_beats"], clip) == 16, "explicit Beat Count wins")
    b.w32(clip + 0xB80, 0)
    b.put_cstr(slots + 3 * 0x170 + 0x5C, "0:/Stems/Bass.wav")
    res.check(b.call(sym["deck_auto_beats"], clip) == 0, "no BPM: stock Auto")
    b.put_cstr(slots + 3 * 0x170 + 0x5C, "0:/Stems/Bass 128bpm.wav")
    # stub contract: r3 = beats, r0-r2/r12/sp and s0-s15 kept
    for reg, val in ((A.UC_ARM_REG_R0, 0xA0), (A.UC_ARM_REG_R1, 0xA1), (A.UC_ARM_REG_R2, 0xA2), (A.UC_ARM_REG_R3, 0xA3),
                     (A.UC_ARM_REG_R4, clip), (A.UC_ARM_REG_R12, 0xAC), (A.UC_ARM_REG_SP, bd.BENCH_SP),
                     (A.UC_ARM_REG_LR, bd.STOP | 1)):
        b.mu.reg_write(reg, val)
    for i in range(16):
        b.mu.reg_write(A.UC_ARM_REG_S0 + i, 0x3F800000 + i)
    b.mu.emu_start(sym["hook_beats"] | 1, bd.STOP, count=20000)
    regs = [b.mu.reg_read(r) for r in (A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3,
                                       A.UC_ARM_REG_R12, A.UC_ARM_REG_SP)]
    vfp = [b.mu.reg_read(A.UC_ARM_REG_S0 + i) for i in range(16)]
    res.check(regs == [0xA0, 0xA1, 0xA2, 640, 0xAC, bd.BENCH_SP], f"hook_beats regs {[hex(r) for r in regs]}")
    res.check(vfp == [0x3F800000 + i for i in range(16)], "hook_beats keeps s0-s15")
    b.unstub(0x080636F0)
    b.unstub(0x0806377C)


def t_ui_knob_stub(bench, res):
    """hook_knob passes the view (r4) as the third argument; a Sample pad turns the stock knob."""
    b = bench.b
    sym = bench.sym
    A = bd.A
    v = WaveView(bench, cellmode=0)
    seen = []
    b.stub(KNOB_TURN, lambda bb: seen.append((bb.arg(0), bb.arg(1))))
    for reg, val in ((A.UC_ARM_REG_R0, VIEW + 0x670), (A.UC_ARM_REG_R1, 77), (A.UC_ARM_REG_R4, VIEW),
                     (A.UC_ARM_REG_SP, bd.BENCH_SP), (A.UC_ARM_REG_LR, bd.STOP | 1)):
        b.mu.reg_write(reg, val)
    b.mu.emu_start(sym["hook_knob"] | 1, bd.STOP, count=20000)
    b.unstub(KNOB_TURN)
    res.check(seen == [(VIEW + 0x670, 77)] and b.mu.reg_read(A.UC_ARM_REG_SP) == bd.BENCH_SP, f"turn {seen}")


def t_deck_bag_size(bench, res):
    """Param lists asked for 64 rows get 96, so a Clip template keeps all deck ids (0x19D..0x1AC)."""
    b = bench.b
    bag, kind = 0xC0610000, 0xC0610100
    old = b.u32(0x2400008C), b.u32(0x24000088)
    b.w32(0x2400008C, 0xC0620000)                         # bump allocator: bench SDRAM scratch
    b.w32(0x24000088, 0xC0700000)
    b.mu.mem_write(bag, b"\x00" * 0x40)
    b.mu.mem_write(kind, b"\x03")                       # Param_BagTemplate case 3 (Clip)
    b.call(0x08093F21, bag, kind)
    b.w32(0x2400008C, old[0])
    b.w32(0x24000088, old[1])
    cap = int.from_bytes(b.mu.mem_read(bag + 0xC, 2), "little")
    n = int.from_bytes(b.mu.mem_read(bag + 10, 2), "little")
    rows = b.mu.mem_read(b.u32(bag + 4), n * 8)
    ids = {int.from_bytes(rows[i * 8:i * 8 + 2], "little") for i in range(n)}
    res.check(cap == 0x60, f"template capacity {cap:#x}")
    missing = [hex(i) for i in range(ID_DECK, ID_DECK + 16) if i not in ids]
    res.check(not missing and n < cap, f"Clip template {n} rows, missing {missing}")


def t_deck_memory(bench, res):
    """DkState stays in 0x38004930..0x38005000, the SRAM4 gap free on this line and on the Bungee / Mix line.
    3.1.T's Song DJ row (DjState, 0x38007400..0x38007A58) is driven from Clip Deck's calls too."""
    b = bench.b
    sym = bench.sym
    writes = []
    dj = (0x38007400, 0x38008000 if "mx_block" in sym else 0x38007A58) if "dj_seq_step" in sym else (0, 0)

    song = "song_step" in sym                  # 3.1.T Song cues: their own block (SG_BASE)

    def hook(mu, access, addr, size, value, data):
        if 0x38000000 <= addr < 0x38010000 and not (0x38004930 <= addr < 0x38005000) \
                and not (dj[0] <= addr < dj[1]) and not (song and SG_BASE <= addr < SG_END):
            writes.append(hex(addr))

    from unicorn import UC_HOOK_MEM_WRITE
    h = b.mu.hook_add(UC_HOOK_MEM_WRITE, hook)
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)
    _deck_set(bench, clip, 2, 14, 4, 8)
    _cue_set(bench, 0x0100)
    b.call(sym["deck_rate"], clip)
    b.call(sym["deck_note_unit"], clip, 0)
    b.call(sym["deck_cue_note"], clip, 37)
    b.mu.hook_del(h)
    res.check(not writes, f"no SRAM4 writes outside the deck block {writes[:5]}")
    res.check(sym["deck_init"] and DK_BASE == 0x38004930, "DK_BASE")
    other = Path(__file__).resolve().parents[2] / "worktrees" / "warp-bungee"
    if other.exists():
        import re
        hits = []
        for src in list((other / "firmware" / "patches").rglob("*.S")) + list((other / "tools").rglob("*.c")) + \
                list((other / "tools").rglob("*.cpp")) + list((other / "tools").rglob("*.h")):
            if "cmsis" in str(src) or "reference" in str(src):
                continue
            for m in re.finditer(r"0x38004[9a-fA-F][0-9a-fA-F]{2}\b", src.read_text(errors="ignore")):
                hits.append(f"{src.name}:{m.group(0)}")
        res.check(not hits, f"Bungee line does not use 0x38004900..0x38004FFF {hits[:5]}")



# ---- Song cues (3.1.T, docs/song-cues.md): a Song scene sends Clip pads to a hot cue

SG_BASE, SG_END = 0x38006400, 0x38007000
SG_PRE_AT, SG_NOW_AT = SG_BASE + 8, SG_BASE + 16    # int64 ticks
SG_CUE = SG_BASE + 0x18              # SgState.cue[32][16]
SG_REQ, SG_ARM, SG_NEXT = SG_BASE + 0x218, SG_BASE + 0x228, SG_BASE + 0x238
SG_NOW, SG_PRE, SG_JUMPED, SG_SKIP = SG_BASE + 0x248, SG_BASE + 0x258, SG_BASE + 0x268, SG_BASE + 0x278
SG_BRUSH, SG_PRE_SCENE = SG_BASE + 0x288, SG_BASE + 0x28A
SG_BTN = SG_BASE + 0x28C             # A..H, Def: 0xA0 each
ID_SCUE = 0x1C0
SONG_KEY = 0x20303
ENGINE_POST = 0x0804C59C
ENGINE_QUEUE = 0x08051738          # Engine_PostParam's queue call
SCENE_SEND = 0x0809C140
SONG_SCENE_FN = 0x0805CDA0
SONG_VT = 0x080F1758
SONG_PANEL, SONG_GRID, SONG_MODE, SONG_SCENE, SONG_BRUSH = 0x11E28, 0x1072C, 0x12194, 0x12180, 0x1218D
PLAYER, LAYER = 0x2001D000, 0x2001DE00


def t_song_sites(bench, res):
    b, sym = bench.b, bench.sym
    img = b.image
    for va, name in ((0x0809450C, "song_template"), (0x080C323A, "song_panel")):
        got = _bl_target(img, va)
        res.check(got == sym[name], f"{va:#x} -> {got and hex(got)} ({name})")
    got = _bw_target(img, 0x0805CDA0)
    res.check(got == sym["hook_song_step"], f"FUN_0805CDA0 entry -> {got and hex(got)} (b.w hook_song_step)")
    for slot, name in ((SONG_VT + 0x30, "song_event"), (SONG_VT + 0x34, "song_event2")):
        got = int.from_bytes(img[slot - bd.BASE:slot - bd.BASE + 4], "little")
        res.check(got == sym[name] | 1, f"Song vt {slot - SONG_VT:#x} -> {got:#x} ({name})")
    res.check(ID_DECK + 16 <= ID_SCUE, f"scue ids past Clip Deck's ({ID_DECK:#x})")


def t_song_registry(bench, res):
    b = bench.b
    if b.u32(REG_PTR) == 0:
        b.call(PARAM_REGISTER_ALL, 0)
    for i in range(16):
        n, t = b.call(PARAM_XML_NAME, ID_SCUE + i), b.call(PARAM_TITLE, ID_SCUE + i)
        res.check(n != 0 and b.cstr(n) == f"scue{i + 1}" and t != 0 and b.cstr(t) == f"Scene Cue {i + 1}:",
                  f"{ID_SCUE + i:#x} xml {n and b.cstr(n)!r} title {t and b.cstr(t)!r}")
    res.check(b.call(0x0808E2D0, ID_SCUE + 5) == 0 and b.call(0x0808E304, ID_SCUE + 5) == 8, "values 0..8")


def t_song_template(bench, res):
    """Stock's scene template (type 0x4E) adds Play Count, then scue1..scue16 at 0."""
    b = bench.b
    adds = []
    b.stub(BAG_ADD, lambda board: adds.append((board.arg(1), board.arg(2))))
    b.mu.mem_write(BAG_SCRATCH, b"\x00" * 0x40)
    b.w32(BAG_SCRATCH + 4, BAG_PAIRS)
    b.mu.mem_write(BAG_PAIRS, b"\x00" * 0x100)
    b.mu.mem_write(BAG_SCRATCH + 10, b"\x00\x00\x20\x00")
    tpl = 0x2001CF00                   # past the rows FUN_08093E6A clears
    b.mu.mem_write(tpl, b"\x4E" + b"\x00" * 7)
    b.call(0x08093F20, BAG_SCRATCH, tpl)
    res.check(adds == [(0xC3, 1)] + [(ID_SCUE + i, 0) for i in range(16)], f"scene template adds {adds}")
    adds.clear()
    b.mu.mem_write(tpl, b"\x03" + b"\x00" * 7)
    b.call(0x08093F20, BAG_SCRATCH, tpl)
    res.check(all(i < ID_SCUE or i >= ID_SCUE + 16 for i, _ in adds), "pad template has none of them")


def t_song_post(bench, res):
    """Engine_PostParam on the song's sequence: scue params go to the SRAM4 table, not to the engine."""
    b, sym = bench.b, bench.sym
    posted = []
    b.stub(ENGINE_QUEUE, lambda board: posted.append(board.arg(1)))
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    res.check(bytes(b.mu.mem_read(SG_CUE, 512)) == b"\x00" * 512, "boot: table empty")
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 3, 5, 0, 7)
    res.check(b.u8(SG_CUE + 7 * 16 + 3) == 5 and not posted, f"scene 8, pad 4: Cue E, kept ({len(posted)} posts)")
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 15, 9, 0, 31)
    res.check(b.u8(SG_CUE + 31 * 16 + 15) == 0, "out of range reads as none")
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE, 2, 0, 32)
    res.check(not posted, "scene 33 (no such scene): dropped")
    b.call(ENGINE_POST, 0x2400A9C0, 0x0100, ID_SCUE, 2, 0, 0)
    res.check(b.u8(SG_CUE) == 0 and not posted, "another key: not stored, not posted")
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, 0xC3, 4, 0, 7)
    res.check(len(posted) == 1, "Play Count still reaches the engine")
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    res.check(b.u8(SG_CUE + 7 * 16 + 3) == 0, "every boot clears the table")


def _player(b, scene, playing=1, advance=1, count=3, wrap=0, start=0, steps=0, step_idx=0, queued=None):
    """The song's player: layer `scene` started at tick `start`, `steps` steps of step_idx (6 = 1 beat).
    queued = (scene, switch tick): a touched scene waiting at +0xC24."""
    b.mu.mem_write(PLAYER, b"\x00" * 0xCB8)
    b.mu.mem_write(LAYER, b"\x00" * 0x48)
    b.w32(LAYER, step_idx)
    b.w32(LAYER + 4, steps)
    b.mu.mem_write(LAYER + 0x38, struct.pack("<q", start))
    b.w32(PLAYER + 0x18, SONG_KEY)
    b.mu.mem_write(PLAYER + 0x30, bytes([playing]))
    b.mu.mem_write(PLAYER + 0x5C, bytes([advance, wrap]))
    if queued:
        b.mu.mem_write(LAYER + 0x48, b"\x00" * 0x48)
        b.mu.mem_write(LAYER + 0x48 + 0x38, struct.pack("<q", queued[1]))
        b.mu.mem_write(LAYER + 0x48 + 0x40, struct.pack("<H", queued[0]))
        b.w32(PLAYER + 0xC24, LAYER + 0x48)
    b.mu.mem_write(PLAYER + 0x60, b"\x01")
    b.w32(PLAYER + 0x64, count)
    b.w32(PLAYER + 0xC20, LAYER)
    b.mu.mem_write(LAYER + 0x40, struct.pack("<H", scene))


def _scene_start(b, scene, **kw):
    """The real FUN_0805CDA0 at step 0 of the song's layer (no items: it only reaches our hook)."""
    _player(b, scene, **kw)
    b.call(SONG_SCENE_FN, PLAYER, 0, 0, 0, 0x2001C000)


def _song_step(b, step):
    """The real FUN_0805CDA0 at a later step of the same layer."""
    b.call(SONG_SCENE_FN, PLAYER, 0, step, 0, 0x2001C000)


def t_song_engine(bench, res):
    b, sym = bench.b, bench.sym
    wrap, unit0, note = sym["deck_wrap_unit"], sym["deck_note_unit"], sym["deck_cue_note"]
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)                         # pad 4 (row 1, col 0), one unit per beat, 16 units
    _cue_set(bench, 0x0100, cues=(6, 0, 11))     # Cue A beat 5, Cue B Off, Cue C beat 10
    for scene, cue in ((0, 1), (1, 3), (2, 2)):  # scene 1: Cue A, 2: Cue C, 3: Cue B (Off on the pad)
        b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, cue, 0, scene)
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 9, 1, 0, 0)

    _scene_start(b, 0)
    res.check(b.u8(SG_REQ + 4) == 1 and b.u8(SG_ARM + 4) == 1 and b.u8(SG_NEXT + 4) == 3,
              "scene 1 start: pad 4 jump + arm Cue A, next scene's Cue C kept cached")
    res.check(b.u8(SG_ARM + 9) == 1 and b.u8(SG_ARM + 5) == 0, "other pads: their own cue, or none")
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(clip + 0x218, 3840)                     # Quant Size 1 bar
    res.check(b.call(wrap, clip, 3, _tick(b, 960 * 3)) == 3 and _cue(b, 4)["pending"] == 5,
              "playing, off the bar: plays on, jump pending at Cue A")
    res.check(b.u8(SG_REQ + 4) == 0, "the request is taken once")
    res.check(b.call(wrap, clip, 4, _tick(b, 3840 * 2)) == 5, "on the bar: jumps to Cue A")
    res.check(b.call(wrap, clip, 6, _tick(b, 3840 * 3)) == 6, "then plays on")
    _scene_start(b, 0)
    res.check(b.call(wrap, clip, 7, _tick(b, 3840 * 4)) == 5, "the scene again (on the bar): jumps at once")
    _scene_start(b, 0)
    b.call(note, clip, 38)                         # a cue note before the boundary
    b.call(wrap, clip, 3, _tick(b, 960 * 3))
    res.check(_cue(b, 4)["pending"] == 10, "a cue note after the scene start wins")
    b.call(unit0, clip, 0)

    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    _scene_start(b, 1)
    res.check(b.call(unit0, clip, 0) == 10, "stopped: armed, a start during the scene begins at Cue C")
    res.check(b.u8(SG_REQ + 4) == 0, "that start is the jump: no request left")
    res.check(b.call(unit0, clip, 0) == 10, "every start during the scene (a sequence retrigger)")
    res.check(b.call(note, clip, 36) == 0 and b.call(unit0, clip, 0) == 5, "a cue note still picks its own cue")
    _scene_start(b, 2)
    res.check(b.call(unit0, clip, 0) == 0, "scene with Cue B, which is Off on the pad: stock start")
    _scene_start(b, 2, advance=0)
    res.check(b.u8(SG_NEXT + 4) == 2, "Loop1: the same scene is next")
    _scene_start(b, 2, count=3, wrap=1)
    res.check(b.u8(SG_NEXT + 4) == 1, "last scene: the first is next (LoopA)")
    _scene_start(b, 2, count=3)
    res.check(b.u8(SG_NEXT + 4) == 0 and b.u8(SG_PRE_SCENE) == 0xFF, "last scene, no LoopA: the song ends, nothing next")
    _scene_start(b, 1)
    b.mu.mem_write(PLAYER + 0x30, b"\x00")         # song stopped
    res.check(b.call(unit0, clip, 0) == 0, "song stopped: a pad press starts as usual")
    _player(b, 0)
    b.mu.mem_write(PLAYER + 0x60, b"\x00")         # an ordinary sequence, not the song
    b.call(SONG_SCENE_FN, PLAYER, 0, 0, 0, 0x2001C000)
    res.check(b.u8(SG_REQ + 4) == 0, "a pad's own sequence at step 0: nothing")
    _player(b, 0)
    b.call(SONG_SCENE_FN, PLAYER, 0, 3, 0, 0x2001C000)
    res.check(b.u8(SG_REQ + 4) == 0, "song at step 3: nothing")
    b.mu.mem_write(clip + 0x28, b"\x01")           # playing again; the scene's pad event (0x8B) arrives
    b.mu.mem_write(clip + 0x260, b"\x01")
    pad_start = sym["song_pad_start"]
    _scene_start(b, 1)
    res.check(b.call(note, clip, 0) == 0, "a voice start with note 0 is never swallowed (stock released the lanes)")
    _scene_start(b, 1)
    res.check(b.call(pad_start, clip) == 1 and _cue(b, 4)["pending"] == 10,
              "playing: the scene's pad event (0x8B) becomes the jump to Cue C")
    res.check(b.u8(SG_REQ + 4) == 0 and b.call(pad_start, clip) == 0, "only once: a later 0x8B is stock's")
    _scene_start(b, 2)
    res.check(b.call(pad_start, clip) == 0, "scene whose cue is Off on the pad: stock's 0x8B")
    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    _scene_start(b, 1)
    res.check(b.call(pad_start, clip) == 0 and b.call(note, clip, 0) == 0 and b.call(unit0, clip, 0) == 10,
              "stopped: stock's pad event starts it, at the cue")
    _cue_set(bench, 0x0100, mode=0, cues=())
    _scene_start(b, 0)
    res.check(b.call(unit0, clip, 0) == 0, "pad without cues: stock")


def t_song_prefetch(bench, res):
    """While a scene plays, the next scene's cue is kept cached, like a pending cue note's."""
    b, sym = bench.b, bench.sym
    seeks = []
    b.stub(SAMPLE_SEEK_IMPL, lambda bd_: seeks.append(bd_.arg(2)))
    b.mu.mem_write(PROV, b"\x00" * 0x5000)
    b.w32(PROV, STREAM_VTABLE)
    clip = _cue_prefetch_clip(bench, PROV)
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, 0, 0, 0)
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, 3, 0, 1)
    _scene_start(b, 0)
    seeks.clear()
    b.call(sym["deck_wrap_unit"], clip, 3, _tick(b, 960 * 3))
    res.check(seeks == [12 * 32768 - 8], f"next scene's Cue C requested {seeks}")
    seeks.clear()
    b.w32(PROV + 0x4FFC, 40)
    b.call(sym["deck_wrap_unit"], clip, 4, _tick(b, 960 * 4))
    res.check(seeks == [], "busy request ring: skipped")


def _song_gui(bench):
    """The real GUI constructor (as t_gui_fx_flow): returns the Song screen object."""
    b = bench.b
    mu = b.mu
    mu.mem_write(0xE000ED88, struct.pack("<I", 0xF << 20))
    for fn in GUI_DP_STUBS + GUI_DRAW_STUBS:
        b.stub(fn, lambda board: 0)
    gui = 0xC2000000
    mu.mem_write(gui, b"\0" * 0x100000)
    b.call(0x0808BE4C, gui)
    raw = bytes(mu.mem_read(gui, 0x100000))
    at = raw.find(struct.pack("<I", SONG_VT))
    while at >= 0 and at & 3:
        at = raw.find(struct.pack("<I", SONG_VT), at + 1)
    return gui + at if at >= 0 else None


def _rect(b, w):
    return struct.unpack("<4i", bytes(b.mu.mem_read(w + 4, 16)))


def t_song_ui(bench, res):
    """Song screen, pad grid, like the sequence grid: pick Def or a cue A-H as the brush, then tap pads;
    an off pad goes on (stock's pad event) with that cue, an on pad goes off. y runs upwards on screen."""
    b, sym = bench.b, bench.sym
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    song = _song_gui(bench)
    res.check(song is not None, "Song screen built")
    if song is None:
        return
    panel = song + SONG_PANEL
    res.check(_rect(b, panel) == (194, 0, 124, 219), f"panel spans two columns {_rect(b, panel)}")
    stock = [_rect(b, panel + 0x34 + i * 0xA0) for i in range(5)]
    res.check(stock == [(256, 176 - 44 * i, 62, 43) for i in range(4)] + [(256, 0, 62, 43)],
              f"stock column unchanged: A at the top, Keep at the bottom {stock}")
    ours = [SG_BTN + i * 0xA0 for i in range(9)]
    want = [(194 + (i // 4) * 62, 176 - (i % 4) * 44, 62, 43) for i in range(8)] + [(194, 0, 62, 43)]
    res.check([_rect(b, w) for w in ours] == want,
              f"A-D left of E-H, A and E at the top, Def by Keep {[_rect(b, w) for w in ours]}")
    res.check([b.u32(w + 0x14) for w in ours] == list(range(0x191, 0x19A)), "button ids 0x191..0x199")
    res.check(all(b.u8(w + 0x30) for w in ours), "hidden on the scene list")
    grid = song + SONG_GRID
    res.check(_rect(b, grid) == (12, 5, 172, 172), f"grid keeps stock's size {_rect(b, grid)}")

    bag = APP_OBJ + 0x29C0
    b.mu.mem_write(bag, b"\x00" * 0x30)
    b.w32(bag + 4, BAG_PAIRS)
    b.mu.mem_write(BAG_PAIRS, b"\x00" * 0x100)
    b.mu.mem_write(bag + 10, b"\x00\x00\x20\x00")
    b.call(sym["song_template"], bag, 0xC3, 1)
    sends, ups = [], []
    b.stub(SCENE_SEND, lambda board: sends.append((board.arg(0), board.arg(1))))
    b.stub(0x080AED14, lambda board: ups.append(bytes(board.mu.mem_read(board.arg(1), 0x18))))   # ui_post_event_up
    b.stub(0x08097884, lambda board: 0)           # scene copy for the stock letters: an empty scene
    b.stub(0x0806381C, lambda board: 0)
    b.stub(0x080B074C, lambda board: 0)           # scene list scroll
    b.mu.mem_write(APP_OBJ + 0x8CC4, b"\x00")
    ev = 0x2001C600
    vt_event, vt_event2 = b.u32(SONG_VT + 0x30) & ~1, b.u32(SONG_VT + 0x34) & ~1

    def post(typ, ident):
        b.mu.mem_write(ev, b"\0" * 0x40)
        b.mu.mem_write(ev, struct.pack("<H", typ))
        b.w32(ev + 0xC, ident)
        return b.call(vt_event, song, ev)

    def refresh():                                 # what stock's refresh leaves after the app's edit
        b.mu.mem_write(ev, b"\0" * 0x40)
        b.call(vt_event2, song, ev)

    def cell(k):
        return chr(b.u8(grid + 0x31 + k)) if b.u8(grid + 0x31 + k) else ""

    def stock_on(k, on=True):
        b.mu.mem_write(grid + 0x31 + k, b"A" if on else b"\x00")

    def lit(w):
        return b.u8(w + 0x96)

    def cue(k):
        return b.call(PARAM_GET, bag, ID_SCUE + k)

    def pad_post():                                # stock's pad event edit: {0xDC, pad | 0x100, on}
        dc = [u for u in ups if struct.unpack_from("<H", u)[0] == 0xDC]
        return [(struct.unpack_from("<I", u, 0xC)[0], struct.unpack_from("<I", u, 0x14)[0]) for u in dc]

    post(0x2B, 0)                                  # scene 1's sequence grid: stock only
    res.check(b.u8(song + SONG_MODE) == 2 and all(b.u8(w + 0x30) for w in ours), "sequence grid: no cue buttons")
    post(0x2C, 0)                                  # the pad grid
    res.check(b.u8(song + SONG_MODE) == 3 and not any(b.u8(w + 0x30) for w in ours), "pad grid: Def, A-H shown")
    res.check(all(b.u8(panel + 0x34 + i * 0xA0 + 0x30) for i in range(4)) and not b.u8(panel + 0x34 + 4 * 0xA0 + 0x30),
              "stock A-D hidden there, Keep shown")
    res.check(lit(ours[8]) == 1 and b.u8(grid + 0x3B0) == 1, "Def is the brush; the grid draws letters")
    post(1, 0x193)                                 # C
    res.check(b.u8(SG_BRUSH) == 3 and lit(ours[2]) == 1 and lit(ours[8]) == 0, "C is the brush, Def dark")
    ups.clear()
    res.check(post(1, 4) == 0 and cue(4) == 3, "tap pad 5 (off): its cue is C")
    res.check(pad_post() == [(0x104, 1)], f"and stock puts its pad event on {pad_post()}")
    res.check(sends == [(APP_OBJ, 0)] and b.u8(APP_OBJ + 0x8CC4) == 1, f"scene re-sent, preset unsaved {sends}")
    stock_on(4)
    refresh()
    res.check(cell(4) == "C", f"the cell shows C ({cell(4)!r})")
    post(1, 0x195)                                 # E
    post(1, 9)
    stock_on(9)
    refresh()
    res.check(cue(9) == 5 and cell(9) == "E" and cue(4) == 3, "pads take different cues")
    post(1, 4)                                     # on: goes off, cue cleared (as the sequence grid)
    stock_on(4, False)
    refresh()
    res.check(cue(4) == 0 and cell(4) == "", "tap an on pad: off, no cue")
    post(1, 0x199)                                 # Def
    res.check(b.u8(SG_BRUSH) == 0 and lit(ours[8]) == 1 and lit(ours[4]) == 0, "Def is the brush")
    post(1, 6)
    stock_on(6)
    refresh()
    res.check(cue(6) == 0 and cell(6) == " ", f"Def: a plain pad event, lit with no letter ({cell(6)!r})")
    post(1, 0x197)                                 # G, then Keep
    post(1, 0x190)
    res.check(b.u8(SG_BRUSH) == 0 and b.u8(song + SONG_BRUSH) == 4 and lit(ours[8]) == 0 and lit(ours[6]) == 0,
              "Keep takes over: our buttons dark")
    ups.clear()
    post(1, 12)
    res.check(cue(12) == 0 and pad_post() == [(0x10C, 2)], f"Keep: stock's Keep event, no cue {pad_post()}")
    b.mu.mem_write(grid + 0x41 + 12, b"\x01")
    refresh()
    res.check(cell(12) == "", "a Keep cell keeps stock's arrow")
    post(1, 0x199)
    res.check(b.u8(song + SONG_BRUSH) == 0 and lit(ours[8]) == 1, "Def turns Keep off")
    post(0x2B, 0)                                  # back to the sequence grid
    res.check(all(b.u8(w + 0x30) for w in ours), "leaving the pad grid hides them")

PAD_EVT_TABLE = 0x08068018            # FUN_08067FA4's switch: words, index = msg type - 2
PAD_8B_STOCK, PAD_EVT_NEXT = 0x080683B4, 0x08067FF4


def t_song_pad_event(bench, res):
    """The Clip pad's msg 0x8B (the scene's pad event): stock releases both lanes, then starts a voice.
    A playing clip with a scene cue waiting skips that case (pending jump instead), else it is stock's."""
    from unicorn import arm_const as A
    b, sym = bench.b, bench.sym
    img = b.image
    word = lambda va: int.from_bytes(img[va - bd.BASE:va - bd.BASE + 4], "little")
    res.check(word(PAD_EVT_TABLE + 4 * (0x8B - 2)) == sym["hook_pad_start"] | 1, "0x8B entry -> hook_pad_start")
    res.check(word(PAD_EVT_TABLE + 4 * (0x8C - 2)) == 0x080682BD and word(PAD_EVT_TABLE + 4 * (4 - 2)) == PAD_EVT_NEXT | 1,
              "0x8C and the rest stay stock")
    res.check(img[0x080683B4 - bd.BASE:0x080683B8 - bd.BASE].hex() == "94f86032" and
              _bl_target(img, 0x080689BE) == 0x08064650 and _bl_target(img, 0x080689AC) == 0x08064650,
              "stock's 0x8B case releases the lanes (FUN_08064650) before the voice start")
    hit = []
    b.stub(PAD_8B_STOCK, lambda board: hit.append("stock"))
    b.stub(PAD_EVT_NEXT, lambda board: hit.append("next"))
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)
    _cue_set(bench, 0x0100, cues=(6, 0, 11))
    b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, 3, 0, 1)

    def run():
        hit.clear()
        b.mu.reg_write(A.UC_ARM_REG_R4, clip)
        b.mu.reg_write(A.UC_ARM_REG_R5, 0x5555)
        b.call(sym["hook_pad_start"], 0x11, 0x22, 0x33, 0x44)
        regs = [b.mu.reg_read(getattr(A, f"UC_ARM_REG_R{i}")) for i in (0, 1, 2, 3, 4, 5)]
        return hit[:], regs

    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    _scene_start(b, 1)
    got, regs = run()
    res.check(got == ["next"] and _cue(b, 4)["pending"] == 10, f"playing + scene cue: skipped, jump pending {got}")
    res.check(regs == [0x11, 0x22, 0x33, 0x44, clip, 0x5555], f"registers kept {[hex(r) for r in regs]}")
    got, _ = run()
    res.check(got == ["stock"], f"no cue waiting: stock's case {got}")
    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    _scene_start(b, 1)
    got, _ = run()
    res.check(got == ["stock"] and b.u8(SG_ARM + 4) == 3, f"stopped: stock starts it (armed at Cue C) {got}")

def t_song_downbeat(bench, res):
    """A scene's jump is planned with the tick its scene starts at: the first grain from that tick takes
    it at once, whether the grain or the scene's own start message comes first, and whatever the
    clip's Quant Size. (On the unit the jump came a bar late about half the time.)"""
    b, sym = bench.b, bench.sym
    wrap, unit0 = sym["deck_wrap_unit"], sym["deck_note_unit"]
    pad_start = sym["song_pad_start"]
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)                         # pad 4, one unit per beat, 16 units
    _cue_set(bench, 0x0100, cues=(6, 11, 13))    # A beat 5, B beat 10, C beat 12
    for scene, cue in ((0, 1), (1, 2), (2, 3)):
        b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, cue, 0, scene)
    b.mu.mem_write(clip + 0x28, b"\x01")
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(clip + 0x218, 3840 * 3)                # Quant Size 3 bars: a plain pending jump would wait
    bar = 3840
    scene_len = 16 * 960                         # 16 steps of a beat: 4 bars

    _scene_start(b, 0, start=0, steps=16, step_idx=6)
    res.check(b.u8(SG_PRE_SCENE) == 1 and struct.unpack("<q", bytes(b.mu.mem_read(SG_PRE_AT, 8)))[0] == scene_len,
              "scene 1 plans scene 2 at its start tick (bar 5)")
    res.check(b.call(wrap, clip, 3, _tick(b, scene_len - 960)) == 3, "the beat before: plays on")
    res.check(b.call(wrap, clip, 4, _tick(b, scene_len)) == 10, "the grain on scene 2's bar jumps to Cue B")
    _player(b, 1, start=scene_len, steps=16, step_idx=6)          # then scene 2's own start arrives
    b.call(SONG_SCENE_FN, PLAYER, 0, 0, 0, 0x2001C000)
    res.check(b.call(pad_start, clip) == 1 and b.call(pad_start, clip) == 0,
              "its pad event (0x8B) is skipped once: no second jump, no retrigger")
    res.check(_cue(b, 4)["pending"] == NONE16 and b.u8(SG_REQ + 4) == 0, "nothing left pending")
    res.check(b.call(wrap, clip, 11, _tick(b, scene_len + 960)) == 11, "then plays on from the cue")

    # the other order: scene 3's start message before the grain on its bar
    t3 = 2 * scene_len
    _player(b, 2, start=t3, steps=16, step_idx=6)
    b.call(SONG_SCENE_FN, PLAYER, 0, 0, 0, 0x2001C000)
    res.check(b.call(pad_start, clip) == 1, "message first: the pad event is skipped")
    res.check(b.call(wrap, clip, 7, _tick(b, t3)) == 12, "and the grain on the bar jumps to Cue C")

    # a late block: the first grain after the bar
    b.call(sym["deck_init"])
    _cue_set(bench, 0x0100, cues=(6, 11, 13))
    for scene, cue in ((0, 1), (1, 2)):
        b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, cue, 0, scene)
    _scene_start(b, 0, start=0, steps=16, step_idx=6)
    res.check(b.call(wrap, clip, 5, _tick(b, scene_len + 960)) == 10, "first grain past the bar: jumps then")

    # a touched scene (queued at +0xC24, switching on the next bar)
    b.call(sym["deck_init"])
    _cue_set(bench, 0x0100, cues=(6, 11, 13))
    for scene, cue in ((0, 1), (1, 2), (2, 3)):
        b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, cue, 0, scene)
    _scene_start(b, 0, start=0, steps=16, step_idx=6)
    b.mu.mem_write(LAYER + 0x48, b"\x00" * 0x48)
    b.mu.mem_write(LAYER + 0x48 + 0x38, struct.pack("<q", bar))
    b.mu.mem_write(LAYER + 0x48 + 0x40, struct.pack("<H", 2))
    b.w32(PLAYER + 0xC24, LAYER + 0x48)
    _song_step(b, 2)
    res.check(b.u8(SG_PRE_SCENE) == 2, "touching scene 3 re-plans at its switch bar")
    res.check(b.call(wrap, clip, 3, _tick(b, bar)) == 12, "the grain on that bar jumps to Cue C")

    # stopped clip: the plan waits, the scene's pad event starts it at the cue
    b.call(sym["deck_init"])
    _cue_set(bench, 0x0100, cues=(6, 11, 13))
    for scene, cue in ((0, 1), (1, 2)):
        b.call(ENGINE_POST, 0x2400A9C0, SONG_KEY, ID_SCUE + 4, cue, 0, scene)
    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    _scene_start(b, 0, start=0, steps=16, step_idx=6)
    _player(b, 1, start=scene_len, steps=16, step_idx=6)
    b.call(SONG_SCENE_FN, PLAYER, 0, 0, 0, 0x2001C000)
    res.check(b.call(pad_start, clip) == 0 and b.call(unit0, clip, 0) == 10,
              "stopped: stock's pad event starts it, at Cue B")
    res.check(b.u8(SG_NOW + 4) == 0, "and that start used the scene's jump")

SG_LATE_OFS, SG_LATE_B79 = SG_BASE + 0x82C, SG_BASE + 0x86C


def t_cue_late_stock(bench, res):
    """Stutter / Repitch: a cue note up to 64 ticks after a Quant boundary on a playing Clip retriggers
    it at once (stock voice start, unquantised for that start) at the cue, its first grain shifted by
    the time since the bar; later notes, bank-2 modes and Quant None keep the pending jump."""
    b, sym = bench.b, bench.sym
    note, unit0, gstart = sym["deck_cue_note"], sym["deck_note_unit"], sym["deck_grain_start"]
    tick = [0]

    def tick_at(bb):
        bb.mu.mem_write(bb.arg(0), struct.pack("<q", tick[0]))
    b.w32(DK_BASE, 0)
    b.call(sym["deck_init"])
    clip = _deck_clip(b)                          # one unit per beat (960 ticks), 32768 frames per unit
    b.stub(0x08069C44, tick_at)
    _cue_set(bench, 0x0100, cues=(6, 11))         # A beat 5, B beat 10
    b.w32(clip + 0x218, 3840)                     # Quant Size 1 bar

    def play(warp):
        b.mu.mem_write(clip + 0x28, b"\x01")
        b.mu.mem_write(clip + 0x260, b"\x01")
        b.mu.mem_write(clip + 0xBB1, bytes([warp]))
        b.mu.mem_write(clip + 0xB79, b"\x01")

    def cue_note(n, t, offset=40):
        tick[0] = t
        b.w32(DK_BASE + 4 + 4, 0x2001C000)        # DkState.save[1]: transport (hook_voice's r1)
        b.w32(DK_BASE + 4 + 8, offset)            # save[2]: sample offset in the block
        return b.call(note, clip, n)

    for warp, name in ((0, "Stutter"), (2, "Repitch")):
        play(warp)
        r = cue_note(37, 3840 * 4 + 32)           # 32 ticks after bar 5
        res.check(r == 0 and _cue(b, 4)["launch"] == 10 and _cue(b, 4)["pending"] == NONE16,
                  f"{name}: late cue note retriggers now at Cue B ({r:#x})")
        res.check(b.u8(clip + 0xB79) == 0 and b.u8(SG_LATE_B79 + 4) == 1, f"{name}: that start unquantised")
        res.check(b.call(unit0, clip, 0) == 10 and b.u8(clip + 0xB79) == 1, f"{name}: note start at the cue, quantise back")
        ofs = struct.unpack("<i", bytes(b.mu.mem_read(SG_LATE_OFS + 16, 4)))[0]
        res.check(ofs == 32 * 32768 // 960, f"{name}: first grain {ofs} frames in (32 ticks)")
        f = b.call(gstart, clip, 10 * 32768) & 0xFFFFFFFF
        res.check(f == 10 * 32768 + 32 * 32768 // 960, f"{name}: first grain starts in phase ({f})")
        res.check(b.call(gstart, clip, 11 * 32768) == 11 * 32768, f"{name}: once only")

    play(0)
    res.check(cue_note(37, 3840 * 4 + 64) == 0, "64 ticks after the bar: still that bar's note")
    b.call(unit0, clip, 0)
    play(0)
    res.check(cue_note(37, 3840 * 4 + 65) == CUE_SWALLOW and _cue(b, 4)["pending"] == 10,
              "65 ticks: pending, the next boundary as before")
    b.call(unit0, clip, 0)
    play(0)
    res.check(cue_note(37, 3840 * 4 + 1500) == CUE_SWALLOW and b.u8(clip + 0xB79) == 1,
              "mid-bar: pending, quantise untouched")
    b.call(unit0, clip, 0)
    for warp in (1, 3, 4, 5):
        play(warp)
        res.check(cue_note(37, 3840 * 4 + 10) == CUE_SWALLOW, f"Warp {warp} (bank 2): pending, deck_cue_late's job")
        b.call(unit0, clip, 0)
    play(0)
    b.w32(clip + 0x218, 0)
    b.w32(clip + 0x21C, 0)
    res.check(cue_note(37, 3840 * 4 + 10) == CUE_SWALLOW, "Quant None: pending (next grain) as before")
    b.call(unit0, clip, 0)
    b.w32(clip + 0x218, 3840)
    b.mu.mem_write(clip + 0x28, b"\x00")
    b.mu.mem_write(clip + 0x260, b"\x00")
    r = cue_note(36, 3840 * 4 + 10)
    res.check(r == 0 and b.u8(clip + 0xB79) == 1 and b.u8(SG_LATE_B79 + 4) == 0,
              "stopped clip: stock's cue launch, quantise untouched")
    play(0)
    cue_note(37, 3840 * 4 + 10)
    res.check(b.u8(clip + 0xB79) == 0, "a retrigger set up ...")
    cue_note(36, 3840 * 4 + 1500)
    res.check(b.u8(clip + 0xB79) == 1, "... that never reached its note start: quantise back on the next note")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--only", help="run only the cases whose name contains this text")
    args = ap.parse_args()
    image = args.image
    probe = open(image, "rb").read()
    sym = {}
    if is_patched(probe):
        p = Path(image).with_suffix(".sym.json")
        if p.exists():
            sym = json.loads(p.read_text())
    folders = "fm_init" in sym
    repitch = "hook_register" in sym
    if folders and repitch:
        kind = "folders+repitch"
    elif folders:
        kind = "folders"
    elif repitch:
        kind = "repitch"
    else:
        kind = "stock"
    res = Result()
    cases = [("list top level", t_list_top), ("load a real preset", t_load_real)]
    write_cases = []
    if folders:
        cases += [
            ("enter a group folder", t_enter_group),
            ("nested load and sample paths", t_nested_load_and_samples),
            ("'..' stops at Presets", t_dotdot_floor),
            ("root xml hidden in groups", t_group_hides_root_xml),
            ("folder with preset.xml loads", t_folder_with_preset_loads),
            ("boot init", t_boot_hook),
            ("folder marker", t_marker),
            ("marker cache on rebuild", t_marker_cache),
            ("BACK goes up", t_back),
            *([("preset ^ button", t_pset_up_button)] if "pset_on_event" in sym else []),
        ]
        if "hook_pathjoin" in sym:
            cases += [("path_join stays in 255 characters", t_pathjoin_bounded)]
        write_cases = [
            ("Save As into a group folder", t_save_as_into_group, TREE),
            ("Save As, same name, other folder", t_save_as_same_name, TREE),
            ("Save As at the top level", t_save_as_top_level, TREE),
            ("Save while browsing another folder", t_save_while_browsing, TREE),
            ("Delete in a group folder", t_delete, DECOY_TREE),
            ("Rename in a group folder", t_rename, DECOY_TREE),
            ("New preset in a group folder", t_new, TREE),
            ("Clean in a group folder", t_clean, DECOY_TREE),
        ]
        if "hook_idle" in sym:
            write_cases += [("Delete / Clean with the marks lost", t_mark_lost_delete, DECOY_TREE)]
        if "hook_save" in sym:
            write_cases += [
                ("Save As on long paths", t_save_as_long_path, LONG_TREE),
                ("Save As onto a group folder", t_save_as_onto_group, SAVE_TREE),
                ("failed saves are shown", t_save_fail_shown, LONG_TREE),
            ]
            if mkcard.exfat_available():
                write_cases += [("Save As on exFAT, deep", t_save_as_exfat, SAVE_TREE, "exfat")]
            else:
                print("exFAT cases skipped: needs hdiutil, newfs_exfat, fsck_exfat (macOS)")
    else:
        cases += [("stock group-row fallback", t_stock_group_fallback)]
    if repitch:
        cases += [
            ("Clip Pos has Repitch", t_repitch_pos_id),
            ("repitch registry name", t_repitch_registry),
            ("repitch set inserts missing id", t_repitch_set_insert),
            ("repitch clip-engine flag", t_repitch_clip_flag),
            ("repitch grain store keeps r3", t_repitch_main_rate_preserves_r3),
            ("slicer grain keeps stack args", t_repitch_slicer_grain_stack),
        ]
    if repitch and "smooth_mix" in sym:             # 3.1.Z's own engines (not the leaf or Mix builds)
        cases += [
            ("Warp enum has Smooth+Capi+Vari+Grain+leaf", t_smooth_enum_labels),
            ("Smooth clip-engine flag", t_smooth_clip_flag),
            ("Smooth grain store sets reset", t_smooth_main_rate_sets_reset),
            ("Smooth lane hook present", t_smooth_lane_hook),
            ("Capi clip-engine flag", t_capi_clip_flag),
            ("Capi grain store sets reset", t_capi_main_rate_sets_reset),
            *([("Capi lane symbols present", t_capi_lane_hook)] if "capi_mix" in sym else []),
            ("Vari clip-engine flag", t_vari_clip_flag),
            ("Vari grain store sets reset", t_vari_main_rate_sets_reset),
            ("Vari lane symbols present", t_vari_lane_hook),
            ("Grain clip-engine flag", t_grain_clip_flag),
            ("Grain rewrites StartGrain window", t_grain_main_rate_rewrites_window),
            ("Smooth state_init clears owner", t_smooth_state_clears_owner),
        ]
    if "deck_limit" in sym:
        kind = kind + "+deck"
        cases += [
            ("Clip Pos has Start/End/Loop", t_deck_pos_ids),
            ("deck registry names", t_deck_registry),
            ("deck bag defaults", t_deck_bag_defaults),
            ("deck region engine math", t_deck_region_engine),
            ("deck engine msg 0x39", t_deck_clip_msg),
            ("deck grain stubs keep stack args", t_deck_grain_stubs),
            ("deck limit stub registers", t_deck_limit_stub),
            ("deck End 512 is the end of a long file", t_deck_long_file),
        ]
    if "deck_cue_note" in sym:
        kind = kind + "+cues"
        cases += [
            ("cue registry and labels", t_cue_registry),
            ("cue bag defaults", t_cue_bag_defaults),
            ("cue params reach SRAM4", t_cue_store),
            ("cue jump engine", t_cue_engine),
            ("loop cannot escape when its markers move", t_deck_loop_escape),
            ("cue voice stub keeps stack args", t_cue_voice_stub),
            ("cue jump prefetches its block", t_cue_prefetch),
            ("cue prefetch uses the stock block request", t_cue_prefetch_stock_seek),
        ]
    if "dj_seq_step" in sym:
        kind = kind + "+songdj"
        cases += [
            ("Song DJ: Clip beatjump and autoloop", t_dj_clip),
            ("Song DJ: pad sequence beatjump and autoloop", t_dj_seq),
            ("Song DJ: song moves inside its scene, walls, loop cut on the scene end", t_dj_song),
            ("Song DJ: Autoloop as Mixxx (closest beat, start kept, halve jumps in)", t_dj_loop_mixxx),
            ("Song DJ: SONG toggles the row, buttons, knobs, labels, blink", t_dj_ui),
            ("Song DJ: real GUI, SONG on the Song screen", t_dj_gui),
        ]
        if "dj_fx_tick" in sym:
            cases += [("Song DJ: DJ FX screen toggles, knobs, Hold moved", t_dj_fx)]
        if "dj_tools_tick" in sym:
            cases += [("Song DJ: Master in the FX row, next to EQ", t_master_row)]
    if "mx_block" in sym:
        cases += [
            ("Song DJ mixer: capture, play, grow, load, repitch, clear", t_mx_loop),
            ("Song DJ mixer: no ring, playing", t_mx_noring),
            ("Song DJ mixer: DJ filters", t_mx_filters),
            ("Song DJ mixer: BPM Global keeps the tempo on load", t_mx_bpm),
            ("Song DJ mixer: BACK, knobs, Loop on the Song screen", t_mx_ui),
        ]
        if "mx_eq" in sym:
            cases += [("Song DJ mixer: DJ FX behind the compressor, routed", t_mx_fx)]
    if "deck_knob_turn" in sym:
        kind = kind + "+editor"
        cases += [
            ("... visible on Clip after stock's update", t_ui_dots_visible),
            ("Clip row: tabs, spares, Slicer restored", t_ui_row_tabs),
            ("popups at Scan's box, touches reach them", t_ui_popups),
            ("Cue letters: at the marker, play, hold Cue + tap to clear / set, knob clear", t_ui_cue_letters),
            ("cue flags on the waveform", t_ui_cue_flags),
            ("Info list prints cues as bar.beat / Off", t_list_value),
            ("titles refresh on every update; no Grid row = 1 bar", t_titles_every_update),
            ("Keys: set cues lit", t_keys_lit),
            ("main knobs: Start / End by Grid, Grid size", t_ui_main_knobs),
            ("Loop popup knobs and buttons", t_ui_loop_popup),
            ("Grid popup: nudge, move, Lines, Raw", t_ui_grid_popup),
            ("Grid markers by Grid size and offset", t_ui_grid_marks),
            ("grid lines and flags drawn for what is on screen", t_ui_wave_draw),
            ("play from the white marker's bar while editing", t_edit_play_cursor),
            ("grid offset moves grains (stub, prefetch)", t_grain_offset),
            ("knob stub passes the view", t_ui_knob_stub),
            ("Keys A-H hooks", t_keys_hooks),
            ("Keys note stub keeps stack arg", t_keys_note_stub),
            ("BPM from file names", t_bpm_names),
            ("Beat Count from a file BPM", t_bpm_auto_beats),
            ("deck param list size", t_deck_bag_size),
            ("deck state memory range", t_deck_memory),
        ]
    if "song_step" in sym:
        kind = kind + "+song"
        cases += [
            ("Song cues: hook sites and vtable", t_song_sites),
            ("Song cues: scue1..16 registered", t_song_registry),
            ("Song cues: scene template", t_song_template),
            ("Song cues: Engine_PostParam keeps them", t_song_post),
            ("Song cues: scene start jumps / arms the pads", t_song_engine),
            ("Song cues: next scene's cue cached", t_song_prefetch),
            ("Song cues: the scene's pad event on a playing Clip jumps", t_song_pad_event),
            ("Song cues: the jump lands on the scene's bar", t_song_downbeat),
            ("late cue note on stock grains retriggers in phase", t_cue_late_stock),
            ("Song cues: pad grid letters and taps", t_song_ui),
        ]
    cases += [("MIDI note path facts", t_midi_path_facts),
              ("waveform bar name length", t_deck_bar_name)]
    slices = "hook_slices_max" in sym
    extra_reads = []
    if slices:
        kind = kind + "+slices"
        cases += [
            ("slice hooks and letter Z", t_slice_hooks),
            ("cell list grows past 128", t_slice_cell_grow),
        ]
        extra_reads.append(("ext attribute restores extra slices", t_slice_xml_load, SLICE_TREE))
    else:
        cases += [("stock cell list stays at 128", t_slice_stock_cap)]
        extra_reads.append(("stock XML keeps 128 tags and ignores ext", t_slice_stock_xml, SLICE_TREE))
    if "hook_idle" in sym:
        extra_reads.append(("marks on a folder of 88 subfolders", t_mark_big_folder, BIG_TREE))
    print(f"image {image} menu letter {menu_letter(probe)!r} ({kind})")
    if "deck_id_v" in sym:                      # 3.1.T: Clip Deck's ids moved past the Mix line's
        global ID_DECK, ID_CUEMODE, ID_CUENOTE, ID_CUE1, ID_GRID, ID_GRIDOFS
        at = int(sym["deck_id_v"], 16) if isinstance(sym["deck_id_v"], str) else sym["deck_id_v"]
        first = int.from_bytes(probe[at - 0x08040000:at - 0x08040000 + 4], "little")
        ID_DECK, ID_CUEMODE, ID_CUENOTE, ID_CUE1 = first, first + 4, first + 5, first + 6
        ID_GRID, ID_GRIDOFS = first + 14, first + 15
    if "rb_pair" in sym:
        cases += [
            ("Rubber absent: label and fallback", t_rubber_absent),
            ("Rubber pairing rejects", t_rubber_pair_rejects),
            ("Rubber lane hook", t_rubber_lane_hook),
            ("Rubber card loader writes bank 2", t_rubber_loader),
            ("Rubber card loader refuses", t_rubber_loader_refuses),
            ("Rubber engine renders (emu build)", t_rubber_engine),
            ("Rubber broken start is left alone", t_rubber_broken_left_alone),
            ("Rubber CPU guard", t_rubber_cpu_guard),
            ("Rubber/Bungee voice ends like a stock lane (emu build)", t_bungee_voice_end),
            ("Rubber/Bungee clock lock, pad feedback, pad switch (emu build)", t_bungee_clock_feedback_switch),
            ("Rubber/Bungee Warp list per pad (choke group)", t_warp_list_per_pad),
            *([("Lineup: Warp list", t_lineup_list), ("Lineup: tempo ratio per Warp", t_lineup_rate),
               ("Lineup: Fill / Chop / Grain lanes (emu build)", t_lineup_lanes),
               ("Lineup: Grain pre-delay and sync, Fill under clock jitter, Stutter on slices (emu build)",
                t_lineup_lanes_timing),
               ("Lineup: Bungee Pitch and Level (emu build)", t_bungee_pitch_level),
               ("Lineup: Fill / Grain / Bungee follow Clip Deck's loop (emu build)", t_lineup_deck),
               ("Lineup: Bungee on the clock at 0.69x, launch and file wrap (emu build)", t_lineup_bungee_sync)]
              if "str_stutter" in sym else []),
            ("Rubber/Bungee sample read retry", t_sd_read_retry),
            ("Rubber cost flat over tempo (emu build)", t_rubber_cost_flat),
            ("Rubber takes over bank 2 only with RBTAKE.TXT", t_rubber_takeover),
            ("Rubber gives bank 2 back with RBFREE.TXT", t_rubber_release),
            ("Rubber stage of a dead boot on the label", t_rubber_stage_label),
            ("Rubber diag: CPU meter on Tools", t_diag_cpu_meter),
            ("Rubber diag: fault recorder", t_diag_fault_record),
            ("Rubber diag: vectors", t_diag_vectors),
            ("Rubber diag: real Tools refresh", t_diag_tools_gui),
        ]
    if "mix_tick" in sym:
        cases += [
            ("Mix hooks and letter", t_mix_hooks),
            ("Mix pads: select / mute / solo", t_mix_pads),
            ("MIX momentary", t_mix_momentary),
            ("header Sel while selecting", t_header_sel),
            ("Mix knobs drive Cutoff/Decay/Sends", t_mix_knobs),
            ("Mix faders and MIX-hold layout", t_mix_layout),
            ("Mix fader touch sets the pad", t_mix_fader_touch),
            ("Mix fader blip from rest", t_mix_blip),
            ("Mix centre detent draw", t_mix_detent_draw),
            ("Mix centre detent snap", t_mix_detent_snap),
            ("MIX button stays on Mix", t_mix_button),
            ("entering MIX press is not a hold", t_mix_enter_press),
            ("encoders stop navigating", t_nav_knobs),
            ("PADS/SEQS held + pad selects only", t_held_select),
            ("Seq length knob and label", t_seq_length),
            ("Rec Len follows Step Count / Step Len", t_seqrec_sync),
            ("looper take", t_looper),
            ("ENV source and Env Trig", t_env_source),
            ("preset list selection", t_pset_first_row),
            ("untouched EQ gets the default bands", t_eq_untouched_defaults),
            ("SEQS held shows the layer panel", t_seq_panel),
            ("PADS held shows the edit panel", t_pads_panel),
            ("EQ tap a dot and drag", t_eq_tap),
            ("FX toggles DJ FX and Return A/B/EQ", t_fx_return),
            ("real GUI: FX / Return / EQ flow", t_gui_fx_flow),
            ("seq rec: params and lists", t_seqrec_params),
            ("seq rec: templates follow the global", t_seqrec_templates),
            ("seq rec: layer-0 routing", t_seqrec_layer),
            ("seq rec: Rec Len sets the length", t_seqrec_setparam),
            ("seq rec: CLR resets untouched", t_seqrec_clear),
            ("seq rec: Custom recording", t_seqrec_record),
            ("Master image patches", t_master_image),
            ("Master params registered", t_master_registry),
            ("Master global template", t_master_template),
            ("Tools page model: five tabs, Master page", t_master_page),
            ("master_comp writes the compressor", t_master_comp),
            ("Master defaults are the stock settings", t_master_defaults_are_stock),
            ("real GUI: Tools -> Master tab", t_master_gui),
        ]
    if args.only:
        cases = [c for c in cases if args.only in c[0]]
        write_cases = [c for c in write_cases if args.only in c[0]]
        extra_reads = [c for c in extra_reads if args.only in c[0]]
    for name, fn in cases:
        run_case(image, name, fn, res)
    for name, fn, tree in extra_reads:
        run_case(image, name, fn, res, tree=tree)
    if write_cases:
        for name, fn, tree, *fs in write_cases:
            run_case(image, name, fn, res, tree=tree, writes=True, fs=(fs or ["fat"])[0])
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
