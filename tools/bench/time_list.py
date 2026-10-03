#!/usr/bin/env python3
"""Measure BuildList sector reads for stock 3.1.9 vs the folder patch.

Uses a 146-row \\Presets tree (the size of the real card) because the card
is not always mounted. Most rows are presets; a few are group folders.
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import board as bd  # noqa: E402
import mkcard  # noqa: E402
from run_tests import Bench, REQUEST_LIST  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
STOCK = ROOT / "firmware" / "bins" / "3.1.9" / "BLACKBOX.bin"
PATCH = ROOT / "firmware" / "patches" / "3.1.9-preset-folders" / "BLACKBOX.BIN"


def big_tree(n_presets=140, n_groups=6):
    tree = {}
    for i in range(n_presets):
        tree[f"Presets/P{i:03d}/preset.xml"] = mkcard.PRESET_XML
    for i in range(n_groups):
        tree[f"Presets/_G{i:02d}/Child/preset.xml"] = mkcard.PRESET_XML
    return tree


def measure(image, card):
    bench = Bench(image, card)
    bench.b.reads = 0
    bench.refresh()
    return bench.b.reads, len(bench.rows())


def main():
    tree = big_tree()
    print(f"tree {len(tree)} files, {140 + 6} top-level Presets rows")
    with tempfile.TemporaryDirectory() as tmp:
        card = os.path.join(tmp, "card.img")
        mkcard.build(card, tree, size_mb=64)
        for label, image in (("stock", STOCK), ("patched", PATCH)):
            reads, rows = measure(str(image), card)
            print(f"{label:8} rows={rows} sector_reads={reads}")


if __name__ == "__main__":
    main()
