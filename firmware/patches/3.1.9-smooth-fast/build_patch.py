#!/usr/bin/env python3
"""Build the 3.1.S image: 3.1.Z (folders+Warp+Vari+2048 slices) with the faster Smooth.

Same cave sources and hook sites as ../3.1.9-folders-repitch; only the outputs and the
version letter differ. Smooth itself lives in tools/stretch/smooth.c.

Writes BLACKBOX.BIN, BLACKBOX.sym.json (cave symbols, used by tools/bench) and cave.dis.
"""

import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE_BUILD = HERE.parent / "3.1.9-folders-repitch" / "build_patch.py"

spec = importlib.util.spec_from_file_location("folders_repitch_build", BASE_BUILD)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

build.HERE = HERE  # cave.dis
build.OUT = HERE / "BLACKBOX.BIN"
build.SYMS = HERE / "BLACKBOX.sym.json"
build.OBJ = HERE / "cave.elf"
build.LETTER = "S"

if __name__ == "__main__":
    build.main()
