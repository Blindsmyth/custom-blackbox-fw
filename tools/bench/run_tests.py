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
        self.image_path = str(image)
        self.b = bd.Board(image, card, allow_writes=allow_writes)
        self.sym = {}
        if self.b.image[VERSION_OFF] != ord("9"):
            symfile = Path(image).with_suffix(".sym.json")
            self.sym = {k: int(v, 16) for k, v in json.loads(symfile.read_text()).items()}
        self.folders = "fm_init" in self.sym
        self.dotdot = "pset_on_event" not in self.sym   # 3.1.Q: ^ button instead of a ".." row
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
    if "rb_pair" in bench.sym:
        # 3.1.R: the list is Stretch / Rubber, so value 1 is Rubber: grain rate unscaled, voice reset
        res.check(stored == 0x40000000, f"rubber leaves the rate {stored:#x}")
        res.check(b.u32(0x38007000 + 20) == 1, "rubber: a new grain asks the voice to reset")
    else:
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
    if "rb_pair" in bench.sym:
        # 3.1.R: value 1 is Rubber; on a Slicer it plays stock grains, unscaled
        res.check(b.u32(clip + 0xE0 + 0x48) == 0x40000000, "rubber leaves grain rate")
        res.check(b.u32(clip + 0xE0 + 0x60) == 0x12345678, "rubber leaves stretch pad")
    else:
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
    res.check(img[VERSION_OFF] in (ord("Q"), ord("R")), f"menu letter is {chr(img[VERSION_OFF])}")


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
    res.check([r[1] for r in xs] == [0, 0, 0, 0] and [r[0] for r in xs] == [0, 104, 216, 268], f"row along the bottom edge {xs}")
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
    res.check(adds == [(0x19B, 0), (SR_DEF, 4), (0x1A0, 2), (0x1A1, 1)],
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
    res.check("Master" in labels and "Main" in labels, f"tab labels {labels}")
    vals = dict(DEFAULTS + [(0xB4, 1)])
    want = [(i, vals[i] & 0xFFFFFFFF) for i in MASTER_IDS]
    res.check(rows[-8:] == want, f"Master list rows (id, value) {[(hex(i), v) for i, v in rows[-8:]]}")
    # the stock path for a Tools tab: App_SetScreen 0x29
    b.call(SET_SCREEN, APP_OBJ, 0x29, 0, 0)
    pump()
    scr, page3 = where()
    res.check(scr == 0x29 and b.u8(page3 + 0xA5E0) == 4, f"screen 0x29 shows Master ({scr:#x})")


# ---- Warp Rubber (3.1.R): pairing, lane hook, card loader, engine ---------------------------

RB_VOICE = 0x38007000
RB_B2 = 0x08100000
RB_BKP = 0x38800000
FLASH2_REGS = (0x52002104, 0x5200210C, 0x52002110, 0x52002114)   # KEYR2 CR2 SR2 CCR2
FLASH1_REGS = (0x52002004, 0x5200200C, 0x52002010, 0x52002014)   # KEYR1 CR1 SR1 CCR1


def rb_payload(bench):
    """The BBRUBBER payload built with this image (BBRUBBER.BIN / BBRUBBER_emu.BIN beside it)."""
    img = Path(bench.image_path)
    p = img.with_name(img.name.replace("BLACKBOX", "BBRUBBER"))
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
    res.check(rb_label(bench) == "Rubber!", f"label {rb_label(bench)!r}")
    res.check(b.call(bench.sym["rb_ready"]) == 0, "rb_ready is 0")
    labels = bench.sym["labels_repitch"]
    got = [b.cstr(b.u32(labels + 4 * i)) for i in range(2)]
    res.check(got == ["Stretch", "Rubber!"], f"Warp labels {got}")
    reg = bench.sym["hook_register"] & ~1
    code = bytes(b.mu.mem_read(reg, 0x40))
    res.check(b"\x02\x24" in code, "Warp enum has 2 values (movs r4, #2)")
    res.check(b.u32(0x24000088) == 0xC3F00000, f"sample stack top {b.u32(0x24000088):#x} (reserve above)")


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
        res.check(rb_state(bench) == 2 and rb_label(bench) == "Rubber!", f"{what}: absent ({rb_state(bench)})")
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
    res.check(rb_state(bench) == 4 and rb_label(bench) == "Rubber!!", f"boot mark: broken, off, {rb_label(bench)!r}")
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
    b.mu.mem_write(clip + 0xBB1, b"\x01")       # Warp Rubber (Stretch / Rubber list)
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
    FakeFile(b, "BBRUBBER.BIN", pay)
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
    FakeFile(b, "BBRUBBER.BIN", pay)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased and not fm.words, "foreign bank 2: no erase, no write")
    res.check(b.u32(RB_B2 + 0x50000) == 0x78563412, "foreign data untouched")
    res.check(rb_label(bench) == "Rubber! x", f"foreign bank 2 named: {rb_label(bench)!r}")
    rb_loader_boot(bench)
    FakeFile(b, "BBRUBBER.BIN", None)
    rb_poll(bench)
    res.check(rb_label(bench) == "Rubber! -", f"no file named: {rb_label(bench)!r}")
    # A damaged file is never written.
    b.mu.mem_write(RB_B2, b"\xff" * 0x100000)
    bad = bytearray(pay)
    bad[len(bad) // 3] ^= 0x40
    rb_loader_boot(bench)
    FakeFile(b, "BBRUBBER.BIN", bad)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(not fm.erased, "damaged file: nothing erased")
    res.check(rb_label(bench) == "Rubber! ?", f"damaged file named: {rb_label(bench)!r}")
    # Failing flash: two tries, then no more.
    b.mu.mem_write(RB_B2, b"\xff" * 0x100000)
    attempts = []
    for _ in range(3):
        rb_loader_boot(bench)
        FakeFile(b, "BBRUBBER.BIN", pay)
        fm = FlashModel(b, fail=True)
        rb_poll(bench)
        fm.close()
        attempts.append(len(fm.erased))
    res.check(attempts[0] > 0 and attempts[1] > 0 and attempts[2] == 0, f"tries per boot {attempts}")
    res.check(rb_label(bench) == "Rubber! t", f"tries used up: {rb_label(bench)!r}")


def t_rubber_engine(bench, res):
    """--emu build: bank 2 boots, pairs, and renders a Clip through stubbed stock buffers."""
    pay = rb_payload(bench)
    if pay is None or "_emu" not in bench.image_path:
        res.check(True, "engine run needs the --emu build (skipped)")
        return
    import math
    b = bench.b
    b.mu.mem_write(RB_B2, bytes(pay))
    rb_bkp(bench)
    b.call(bench.sym["rb_pair"])
    res.check(rb_state(bench) == 3 and rb_label(bench) == "Rubber..", f"paired, not started: {rb_label(bench)!r}")
    sp = []
    from unicorn import UC_HOOK_CODE
    h = b.mu.hook_add(UC_HOOK_CODE, lambda mu, a, sz, d: sp.append(mu.reg_read(bd.A.UC_ARM_REG_SP)),
                      begin=bench.sym["rubber_boot"] & ~1, end=bench.sym["rubber_boot"] & ~1)
    b.call(bench.sym["rb_load_poll"], limit=2_000_000_000)
    b.mu.hook_del(h)
    res.check(rb_state(bench) == 1 and rb_label(bench) == "Rubber", f"started from the disk poll: {rb_label(bench)!r}")
    res.check(sp and 0x30040000 <= sp[0] <= 0x30048000, f"rubber_boot runs on Rubber's own stack ({sp[0]:#x})" if sp else "rubber_boot ran")
    res.check(b.u32(RB_BKP + 16) == 0 and b.u32(RB_BKP + 20) == 0, "boot mark and stage cleared after the start")
    if rb_state(bench) != 1:
        return
    sr, n_src = 48000, 48000 * 2
    src = [0.4 * math.sin(2 * math.pi * 220 * i / sr) * (1.0 if (i // 6000) % 2 == 0 else 0.3) for i in range(n_src)]
    sample, clip, mixbuf, ctx, scratch = 0x2001C000, 0x2001C800, 0x2001E000, 0x2001E100, 0x2001E200
    planes = (0x30040000, 0x30041000)
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

    for addr, fn in ((0x08063634, pcm_read), (0x080636F0, lambda bb: n_src), (0x08063790, lambda bb: 1),
                     (0x0805F3A4, lambda bb: scratch), (0x0804D8DC, buf_clear), (0x0804D8E8, lambda bb: 256),
                     (0x0804D8D0, lambda bb: 0), (0x0804D9C0, buf_planes), (0x080762D8, lambda bb: 0),
                     (0x0804DA6C, buf_mix)):
        b.stub(addr, fn)
    b.mu.mem_write(clip, b"\x00" * 0xC00)
    b.w32(clip + 0x24, sample)
    b.mu.mem_write(clip + 0xBB4, struct.pack("<f", 0.8))
    b.mu.mem_write(clip + 0x260, b"\x01")
    b.w32(RB_VOICE + 20, 1)                     # launch
    for _ in range(60):
        b.call(bench.sym["rb_mix"], clip, clip + 0x260, mixbuf, ctx, limit=400_000_000)
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
    FakeFile(b, "BBRUBBER.BIN", pay)
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
    res.check(rb_label(bench) == "Rubber! 32", f"label {rb_label(bench)!r}")
    res.check(b.u32(RB_BKP + 20) == 0, "stage cleared")
    b.call(bench.sym["rb_pair"])
    res.check(rb_label(bench) == "Rubber!", f"next clean boot: {rb_label(bench)!r}")


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
    for _ in range(200):
        b.stub(0x08088756, receive)
        b.call(bench.sym["cpu_wait"], 0, 0, 0, 0)
        clock[0] += busy
        b.w32(0xE0001004, clock[0])
    mid, _ = diag_tools_label(bench)
    res.check(mid == "39/39% R120", f"with Rubber's own share: {mid!r}")
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
    res.check(rb_label(bench) == "Rubber! 11", f"Rubber label names the stage: {rb_label(bench)!r}")
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
    FakeFile(b, "BBRUBBER.BIN", pay)
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(rb_label(bench) == "Rubber! x" and not fm.erased and not fm.words, f"without RBTAKE.TXT: left alone, {rb_label(bench)!r}")
    rb_loader_boot(bench)
    FakeFile(b, "BBRUBBER.BIN", pay, others={"RBTAKE.TXT"})
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
    FakeFile(b, "BBRUBBER.BIN", pay, others={"RBFREE.TXT"})
    fm = FlashModel(b)
    rb_poll(bench)
    fm.close()
    res.check(rb_state(bench) == 2 and rb_label(bench).endswith(" f"), f"Rubber off, {rb_label(bench)!r}")
    res.check(bytes(b.mu.mem_read(RB_B2, 0xE0000)) == b"\xff" * 0xE0000, "sectors 0-6 blank (blackbox-mod's loader takes it)")
    res.check(not fm.words and not fm.reset and not fm.bad, "nothing written, no restart")
    rb_loader_boot(bench)
    FakeFile(b, "BBRUBBER.BIN", pay, others={"RBFREE.TXT"})
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
    cost[0] = 2_300_000                         # 90 %
    for i in range(31):
        b.call(bench.sym["rb_mix"], 1, 2, 3, 4)
    res.check(rb_state(bench) == 1, "31 slow blocks: still on")
    b.call(bench.sym["rb_mix"], 1, 2, 3, 4)
    res.check(rb_state(bench) == 5 and rb_label(bench) == "Rubber! cpu", f"32nd: off, {rb_label(bench)!r}")
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
    m = re.search(r"R(\d+)$", mid or "")
    res.check(m is not None and 70 <= int(m.group(1)) <= 90, f"cost still on Tools: {mid!r}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--only", help="run only the cases whose name contains this text")
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
            *([("preset ^ button", t_pset_up_button)] if "pset_on_event" in sym else []),
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
    for name, fn in cases:
        run_case(image, name, fn, res)
    if write_cases:
        for name, fn, tree in write_cases:
            run_case(image, name, fn, res, tree=tree, writes=True)
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
