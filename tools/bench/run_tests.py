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
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import board as bd  # noqa: E402
import mkcard  # noqa: E402

VERSION_OFF = 0x8F294
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

TREE = dict(mkcard.DEFAULT_TREE)
TREE["Root.xml"] = mkcard.PRESET_XML  # legacy root preset: listed at the top level only


class Bench:
    def __init__(self, image, card, allow_writes=False):
        self.card_path = card
        self.b = bd.Board(image, card, allow_writes=allow_writes)
        self.sym = {}
        if self.b.image[VERSION_OFF] != ord("9"):
            symfile = Path(image).with_suffix(".sym.json")
            self.sym = {k: int(v, 16) for k, v in json.loads(symfile.read_text()).items()}
        self.folders = "fm_init" in self.sym
        self.repitch = "hook_register" in self.sym
        self.patched = self.folders
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

    def back(self):
        """Press BACK on the preset screen. Returns True when the screen was left."""
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


def run_case(image, name, fn, res, tree=None, writes=False):
    print(f"[{name}]")
    with tempfile.TemporaryDirectory() as tmp:
        card = os.path.join(tmp, "card.img")
        # Save As only copies samples with 100 MB to spare
        mkcard.build(card, tree or TREE, size_mb=160 if writes else 64)
        bench = Bench(image, card, allow_writes=writes)
        try:
            fn(bench, res)
        except Exception as e:  # a crash in emulation is a failure, not a bench error
            res.check(False, f"no emulation fault ({e})")
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
    res.check(clip == [0x8D, 0xD2, 0x47, 0x94, ID_REPITCH, 0], f"Clip Pos ids {clip}")
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
    res.check(stored == 0x3F800000, f"repitch rate {stored:#x}")


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
    res.check(b.u32(clip + 0xE0 + 0x48) == 0x3F800000, "repitch scales grain rate")
    res.check(b.u32(clip + 0xE0 + 0x60) == 0, "repitch clears stretch pad")
    res.check(b.mu.reg_read(A.UC_ARM_REG_R0) == 2, "repitch keeps grain index")


# ---- patched-only tests
def t_enter_group(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    res.check(not bench.load_ok(), "no preset loaded for a group folder")
    res.check(not bench.screen, "stays on the preset screen")
    res.check(bench.browse() == "Presets\\_Test", f"browse path {bench.browse()!r}")
    rows = bench.rows()
    res.check(rows == ["..", "/Sub", "One", "Two"], f"group rows {rows}")
    res.check(any(e[0] == 0x11 for e in bench.events), "list-done event posted (UI refresh)")


def t_nested_load_and_samples(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    bench.press_load("/Sub")
    res.check(bench.rows() == ["..", "Deep"], f"nested rows {bench.rows()}")
    bench.press_load("Deep")
    res.check(len(bench.load_ok()) == 1, "Deep loads from Presets\\_Test\\Sub")
    res.check(bench.current() == "Deep", f"current preset {bench.current()!r}")
    res.check(bench.base() == "Presets\\_Test\\Sub", f"loaded-preset folder {bench.base()!r}")
    res.check(bench.join("Deep") == "Presets\\_Test\\Sub\\Deep", f"sample path base {bench.join('Deep')!r}")
    bench.press_load("..")
    bench.press_load("..")
    res.check(bench.browse() == "Presets", f"back at top {bench.browse()!r}")
    res.check(bench.join("Deep") == "Presets\\_Test\\Sub\\Deep",
              "sample path still uses the loaded preset's folder after browsing away")


def t_dotdot_floor(bench, res):
    bench.refresh()
    bench.press_load("/_Test")
    bench.press_load("..")
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
    res.check(bench.rows() == ["..", "/Sub", "One", "Two"], f"nested group marked {bench.rows()}")


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
    res.check(bench.rows() == ["..", "/Sub", "One", "Two"], f"list rebuilt {bench.rows()}")
    res.check(not bench.back(), "BACK again stays on the screen")
    res.check(bench.browse() == "Presets", f"BACK reaches {bench.browse()!r}")
    res.check(bench.back(), "BACK at the top leaves the screen")
    res.check(bench.browse() == "Presets", "browse path stays at Presets")
    # Hardware BACK is HandleInput type 7. hook_type7 is a bl that can leave
    # that function's frame, so the bench only calls folder_back.
    bench.press_load("/_Test")
    res.check(bench.b.call(bench.sym["folder_back"]) == 0, "folder_back pops a level")
    bench.pump()
    res.check(bench.browse() == "Presets", f"folder_back browse {bench.browse()!r}")
    res.check(bench.b.call(bench.sym["folder_back"]) == 1, "folder_back at the top is a no-op")
    bench.press_load("/_Test")
    bench.b.reads = 0
    bench.press_load("..")
    res.check(bench.browse() == "Presets", f"cached BACK browse {bench.browse()!r}")
    res.check(bench.b.reads < 100, f"cached BACK reads {bench.b.reads}")


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
    bench.press_load("..")
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
    res.check(bench.rows() == ["..", "/Sub2", "One", "Three"], f"list rebuilt with the marker {bench.rows()}")
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
                            (0x080F0F40, "mix_child_event", 0x080B5A4D),
                            (0x080F0F44, "mix_on_event", 0x080B5C71)):
        word = struct.unpack_from("<I", img, va - bd.BASE)[0]
        res.check(word == bench.sym[name] | 1, f"Mix vtable {va:#x} -> {name} ({word:#x}, stock {stock:#x})")
    res.check(img[VERSION_OFF] == ord("Q"), "menu letter is Q")


def t_mix_pad_plays(bench, res):
    b = _mix_setup(bench, build=False)
    posts = []
    b.stub(UI_POST_UP, lambda board: posts.append(struct.unpack("<H", board.mu.mem_read(board.arg(1), 2))[0]))
    fn = bench.sym["mix_child_event"]
    r = b.call(fn, MIX_OBJ, _mix_ev(b, 3, key=0x12))
    res.check(r == 1 and not posts, f"pad press propagates to App_HandleUiEvent (r={r}, posts={posts})")
    r = b.call(fn, MIX_OBJ, _mix_ev(b, 4, key=0x12))
    res.check(r == 1 and not posts, f"pad release propagates (r={r})")
    b.mu.mem_write(BTN_MIX, b"\x00")
    r = b.call(fn, MIX_OBJ, _mix_ev(b, 3, key=0x12))
    res.check(r == 0 and posts == [0x63], f"MIX held: press only selects (r={r}, posts={posts})")
    r = b.call(fn, MIX_OBJ, _mix_ev(b, 4, key=0x12))
    res.check(r == 0 and posts == [0x63], f"MIX held: release swallowed (r={r})")
    b.mu.mem_write(BTN_MIX, b"\x01")
    b.mu.mem_write(MIX_OBJ + 0x1E40, b"\x01")
    posts.clear()
    r = b.call(fn, MIX_OBJ, _mix_ev(b, 3, key=0x12))
    res.check(r == 0 and posts == [0x63], "Mute mode keeps the stock handler")


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
    res.check(_slider_ids(b) == MIX_HELD, f"knobs follow the MIX layer {_slider_ids(b)}")
    res.check(not any(_fader(b, n)["hidden"] for n in MIX_FADERS), "all four faders shown in both layers")
    b.mu.mem_write(BTN_MIX, b"\x01")
    _mix_tick(bench)
    res.check({n: _fader(b, n)["id"] for n in MIX_FADERS} == MIX_MAIN, "MIX released: main layer back")
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
                res.check(len(moved) == 1, f"{name} encoder 3 still switches the side panel")
    moved.clear()
    b.call(0x080B9BA4, obj, 1)
    res.check(not moved, "scroll-list encoder row select is off")
    img = bench.b.image
    res.check(img[0x080B9A3C - bd.BASE: 0x080B9A3C - bd.BASE + 4] == open(STOCK_IMAGE, "rb").read()[0x080B9A3C - bd.BASE: 0x080B9A3C - bd.BASE + 4],
              "list value knob (FUN_080B9A3C) untouched")


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


def t_seq_length(bench, res):
    b = _mix_setup(bench, build=False)
    params = {0xC0: 1, 0x88: 0, 0x85: 10, 0x86: 16}                 # 16 x 1/16 = 1 bar
    events = [(0, 1), (960, 2), (1920, 3), (3000, 4)]
    log = _seq_stubs(b, params, events)
    b.call(bench.sym["seq_halve"], APP_OBJ, 0x23)
    res.check(log["set"] == [(0x86, 8)] and log["snap"] == 1, f"halve sets 8 steps with an undo snapshot {log['set']}")
    res.check(sorted(log["del"]) == [3, 4], f"halve drops events at or past 8 x 240 ticks {log['del']}")
    res.check(log["push"] == [1], f"halve pushes the current layer {log['push']}")
    seq = 0x30020000
    b.mu.mem_write(seq, b"\0" * 0x4000)
    b.mu.mem_write(seq + 0x1A90, b"\x23")
    for k in log:
        log[k] = [] if isinstance(log[k], list) else 0
    b.w32(UWTICK, 10000)
    b.call(bench.sym["seq_on_event"], seq, _mix_ev(b, 0x32, idx=2, val=1))
    res.check(log["double"] == [0x23], f"TR knob right doubles {log['double']}")
    b.w32(UWTICK, 10100)
    b.call(bench.sym["seq_on_event"], seq, _mix_ev(b, 0x32, idx=2, val=-1))
    res.check(not log["set"], "a second turn within 250 ms is ignored")
    b.w32(UWTICK, 10400)
    b.call(bench.sym["seq_on_event"], seq, _mix_ev(b, 0x32, idx=2, val=-1))
    res.check(log["set"] == [(0x86, 8)], f"TR knob left halves {log['set']}")
    res.check(log["text"][-1:] == ["1 bar"], f"bar label refreshed after a change {log['text']}")
    for n, want in ((16, "1 bar"), (8, "0.5"), (64, "4 bar"), (6, "0.4")):
        params[0x86] = n
        log["text"].clear()
        b.call(bench.sym["seq_bar_label"], seq)
        res.check(log["text"] == [want], f"{n} x 1/16 shows {want!r} ({log['text']})")


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
    ids = [b.u32(page + 0x70 + i * 0xA0 + 0x14) for i in range(3)]
    sel = [b.u8(page + 0x70 + i * 0xA0 + 0x96) for i in range(3)]
    res.check(ids == [0x7A2, 0x7A1, 0x7A0] and sel == [0, 0, 1], f"tabs EQ/B/A (drawn A | B | EQ) with A lit {ids} {sel}")
    screens.clear()
    ev = _mix_ev(b, 1)
    b.w32(ev + 0xC, 0x7A1)
    r = b.call(bench.sym["fxret_child_event"], page, ev)
    res.check(r == 0 and screens == [0x23] and b.u16(APP_OBJ + 0x28) == 0x310, f"B shows Reverb {screens}")
    screens.clear()
    b.w32(ev + 0xC, 0x7A2)
    b.call(bench.sym["fxret_child_event"], page, ev)
    res.check(screens == [0x36], f"EQ opens the EQ page {screens}")
    b.mu.mem_write(APP_OBJ + 0x28, struct.pack("<H", 0x37))
    screens.clear()
    b.call(bench.sym["fx_to_return"], APP_OBJ, 0x30, 0, 0)
    res.check(b.u16(APP_OBJ + 0x28) == 0x310, "FX returns to the last return shown (B)")


def t_mix_button(bench, res):
    b = _mix_setup(bench, build=False)
    calls = []
    b.stub(SET_SCREEN, lambda board: calls.append(board.arg(1)))
    b.call(bench.sym["mix_btn_screen"], APP_OBJ, 0x2F, 0, 0)
    res.check(calls == [], "MIX on Mix stays on Mix")
    b.call(bench.sym["mix_btn_screen"], APP_OBJ, 0x2E, 0, 0)
    res.check(calls == [0x2E], f"MIX elsewhere opens Mix {calls}")


def t_info_momentary(bench, res):
    b = _mix_setup(bench)
    calls = []
    b.stub(SET_SCREEN, lambda board: calls.append(board.arg(1)))
    _mix_tick(bench)
    b.mu.mem_write(MIX_OBJ + 0x1E40, b"\x01")       # Mute mode
    b.w32(UWTICK, 1000)
    b.call(bench.sym["info_to_mute"], APP_OBJ, 0x2F, 0, 0)
    res.check(calls == [0x2F], f"INFO on Mix opens Mute {calls}")
    b.mu.mem_write(BTN_INFO, b"\x00")
    b.w32(UWTICK, 1200)
    _mix_tick(bench)
    res.check(b.u32(MX_STATE + 8) == 1, "short hold is still a toggle")
    b.w32(UWTICK, 1450)
    _mix_tick(bench)
    res.check(b.u32(MX_STATE + 8) == 2, "held past 400 ms becomes momentary")
    b.mu.mem_write(BTN_INFO, b"\x01")
    _mix_tick(bench)
    res.check(calls == [0x2F, 0x2E], f"release after a long hold returns to Mix {calls}")
    calls.clear()
    b.w32(UWTICK, 5000)
    b.call(bench.sym["info_to_mute"], APP_OBJ, 0x2F, 0, 0)
    b.w32(UWTICK, 5100)
    _mix_tick(bench)
    res.check(calls == [0x2F] and b.u32(MX_STATE + 8) == 0, f"short press stays in Mute {calls}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    args = ap.parse_args()
    image = args.image
    probe = open(image, "rb").read()
    sym = {}
    if probe[VERSION_OFF] != ord("9"):
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
    print(f"image {image} version byte {chr(probe[VERSION_OFF])!r} ({kind})")
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
        ]
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
    if "mix_tick" in sym:
        cases += [
            ("Mix hooks and letter", t_mix_hooks),
            ("Mix pads play unless MIX held", t_mix_pad_plays),
            ("Mix knobs drive Cutoff/Decay/Sends", t_mix_knobs),
            ("Mix faders and MIX-hold layout", t_mix_layout),
            ("Mix fader touch sets the pad", t_mix_fader_touch),
            ("Mix fader blip from rest", t_mix_blip),
            ("Mix centre detent draw", t_mix_detent_draw),
            ("Mix centre detent snap", t_mix_detent_snap),
            ("MIX button stays on Mix", t_mix_button),
            ("encoders stop navigating", t_nav_knobs),
            ("PADS/SEQS held + pad selects only", t_held_select),
            ("Seq length knob and bar label", t_seq_length),
            ("EQ tap a dot and drag", t_eq_tap),
            ("FX toggles DJ FX and Return A/B/EQ", t_fx_return),
            ("INFO momentary Mute", t_info_momentary),
        ]
    for name, fn in cases:
        run_case(image, name, fn, res)
    if write_cases:
        for name, fn, tree in write_cases:
            run_case(image, name, fn, res, tree=tree, writes=True)
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
