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
image is checked with fsck.fat -n afterwards. Tests of the writing commands (Save As,
Delete, Rename, New, Clean) run with writes allowed and check the exact set of files
that changed instead.
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
    # Hardware BACK is event type 0x7f (not type-2 key 0x7f). folder_back is shared.
    bench.press_load("/_Test")
    res.check(bench.b.call(bench.sym["folder_back"]) == 0, "folder_back pops a level")
    bench.pump()
    res.check(bench.browse() == "Presets", f"folder_back browse {bench.browse()!r}")
    res.check(bench.b.call(bench.sym["folder_back"]) == 1, "folder_back at the top is a no-op")


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
    for name, fn in cases:
        run_case(image, name, fn, res)
    if patched:
        for name, fn, tree in write_cases:
            run_case(image, name, fn, res, tree=tree, writes=True)
    print(f"\n{res.passed} passed, {res.failed} failed")
    sys.exit(1 if res.failed else 0)


if __name__ == "__main__":
    main()
