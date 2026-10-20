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

TREE = dict(mkcard.DEFAULT_TREE)
TREE["Root.xml"] = mkcard.PRESET_XML  # legacy root preset: listed at the top level only


class Bench:
    def __init__(self, image, card, allow_writes=False):
        self.card_path = card
        self.b = bd.Board(image, card, allow_writes=allow_writes)
        self.sym = {}
        if is_patched(self.b.image):
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
    res.check(clip[:5] == [0x8D, 0xD2, 0x47, 0x94, ID_REPITCH] and clip[5] in (0, 0x19D, 0x1A1),
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
    res.check(seen.get("lr", 0) | 1 == bd.STOP | 1, f"{hook} return address")
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
REF = 0xC0500000                # fake sample ref (0x34 bytes): +0x20 provider, +0x24 wave id
PROV = 0xC0500100               # fake provider for the stubbed-seek case (vtable word only)
MGR = 0xC0100000                # fake streamer manager in SDRAM (mapped, unused by the bench)


def _cue_prefetch_clip(bench, provider, wave=7):
    b = bench.b
    b.call(bench.sym["deck_init"])
    clip = _deck_clip(b)                       # 32768 frames per unit, row 1 col 0
    b.mu.mem_write(REF, b"\x00" * 0x34)
    b.w32(REF + 0x20, provider)
    b.w32(REF + 0x24, wave)
    b.w32(clip + 0x24, REF)
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
    missing = [hex(i) for i in range(0x19D, 0x1AD) if i not in ids]
    res.check(not missing and n < cap, f"Clip template {n} rows, missing {missing}")


def t_deck_memory(bench, res):
    """DkState stays in 0x38004930..0x38005000, the SRAM4 gap free on this line and on the Bungee / Mix line."""
    b = bench.b
    sym = bench.sym
    writes = []

    def hook(mu, access, addr, size, value, data):
        if 0x38000000 <= addr < 0x38010000 and not (0x38004930 <= addr < 0x38005000):
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
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
            ("Warp enum has Smooth+Capi+Vari+Grain+leaf", t_smooth_enum_labels),
            ("Smooth clip-engine flag", t_smooth_clip_flag),
            ("Smooth grain store sets reset", t_smooth_main_rate_sets_reset),
            ("Smooth lane hook present", t_smooth_lane_hook),
            ("Capi clip-engine flag", t_capi_clip_flag),
            ("Capi grain store sets reset", t_capi_main_rate_sets_reset),
            ("Capi lane symbols present", t_capi_lane_hook),
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
    print(f"image {image} menu letter {menu_letter(probe)!r} ({kind})")
    for name, fn in cases:
        run_case(image, name, fn, res)
    for name, fn, tree in extra_reads:
        run_case(image, name, fn, res, tree=tree)
    if write_cases:
        for name, fn, tree in write_cases:
            run_case(image, name, fn, res, tree=tree, writes=True)
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
