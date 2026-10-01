#!/usr/bin/env python3
"""Bench tests for BLACKBOX.BIN 3.1.9, stock or with the preset-folder patch.

    .venv/bin/python tools/bench/run_tests.py --image firmware/bins/3.1.9/BLACKBOX.bin
    .venv/bin/python tools/bench/run_tests.py --image firmware/patches/3.1.9-preset-folders/BLACKBOX.BIN

The UI side is modelled at the PresetMgr boundary: a Load press, the pcmStreamer
command dispatcher, and the UI event pop. SessionMgr_LoadBank and the screen switch
are stubbed and recorded; a recorded LoadBank becomes a real PresetMgr_RequestLoad,
which is what the stock LoadBank does after clearing the pads.

Every test runs on a fresh card image. The card is write-protected (disk_write
returns RES_WRPRT and is recorded), so any attempted write fails the test, and the
image is checked with fsck.fat -n afterwards.
"""
import argparse
import json
import os
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
    def __init__(self, image, card):
        self.b = bd.Board(image, card)
        self.patched = self.b.image[VERSION_OFF] != ord("9")
        self.sym = {}
        if self.patched:
            symfile = Path(image).with_suffix(".sym.json")
            self.sym = {k: int(v, 16) for k, v in json.loads(symfile.read_text()).items()}
        self.loadbank = []
        self.screen = []
        self.events = []
        self.b.stub(LOAD_BANK, self._loadbank)
        self.b.stub(SCREEN_SWITCH, lambda b: self.screen.append((b.arg(0), b.arg(1))))
        self.b.boot()
        if self.patched:
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


def run_case(image, name, fn, res):
    print(f"[{name}]")
    with tempfile.TemporaryDirectory() as tmp:
        card = os.path.join(tmp, "card.img")
        mkcard.build(card, TREE, size_mb=64)
        bench = Bench(image, card)
        try:
            fn(bench, res)
        except Exception as e:  # a crash in emulation is a failure, not a bench error
            res.check(False, f"no emulation fault ({e})")
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
    res.check(rows == ["_Test", "Alpha", "Beta", "Root", "Zeta Kit"], f"top-level rows {rows}")
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


# ---- patched-only tests
def t_enter_group(bench, res):
    bench.refresh()
    bench.press_load("_Test")
    res.check(not bench.load_ok(), "no preset loaded for a group folder")
    res.check(not bench.screen, "stays on the preset screen")
    res.check(bench.browse() == "Presets\\_Test", f"browse path {bench.browse()!r}")
    rows = bench.rows()
    res.check(rows == ["..", "One", "Sub", "Two"], f"group rows {rows}")
    res.check(any(e[0] == 0x11 for e in bench.events), "list-done event posted (UI refresh)")


def t_nested_load_and_samples(bench, res):
    bench.refresh()
    bench.press_load("_Test")
    bench.press_load("Sub")
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
    bench.press_load("_Test")
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
    bench.press_load("_Test")
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    args = ap.parse_args()
    image = args.image
    probe = open(image, "rb").read()
    patched = probe[VERSION_OFF] != ord("9")
    print(f"image {image} version byte {chr(probe[VERSION_OFF])!r} ({'patched' if patched else 'stock'})")
    res = Result()
    cases = [("list top level", t_list_top), ("load a real preset", t_load_real)]
    if patched:
        cases += [
            ("enter a group folder", t_enter_group),
            ("nested load and sample paths", t_nested_load_and_samples),
            ("'..' stops at Presets", t_dotdot_floor),
            ("root xml hidden in groups", t_group_hides_root_xml),
            ("folder with preset.xml loads", t_folder_with_preset_loads),
            ("boot init", t_boot_hook),
        ]
    else:
        cases += [("stock group-row fallback", t_stock_group_fallback)]
    for name, fn in cases:
        run_case(image, name, fn, res)
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
