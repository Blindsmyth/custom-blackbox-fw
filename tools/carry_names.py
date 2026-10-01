#!/usr/bin/env python3
"""Carry function names from a named version to other versions via string anchors.

  carry_names.py <src_ver> <dst_ver> [...]

A function in <src_ver> is matched to a function in <dst_ver> when both are the only
users of the same string text, and that string is used by exactly one function on each
side. Matches are then extended one step through the call graph: if a matched pair has
exactly one unmatched named callee on the source side and exactly one unmatched callee
with the same caller-count rank on the destination side, they are paired too.

Writes docs/symbols/<dst_ver>.auto.csv (never overwrites the hand-made <ver>.csv) and
prints a summary. Input: ~/GhidraProjects/out/<ver>/{functions,strings,calls}.tsv.
"""
import collections
import os
import sys

PROJ = os.environ.get("GHIDRA_PROJ", os.path.expanduser("~/GhidraProjects"))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def binpath(ver):
    import glob
    hits = glob.glob(os.path.join(ROOT, "firmware", "bins", ver, "BLACKBOX.*"))
    return hits[0]


def shapes(ver, sizes):
    """Hash of each function's mnemonic/register sequence with immediates and addresses removed."""
    import hashlib
    import re
    import capstone
    data = open(binpath(ver), "rb").read()
    base = 0x08040000
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS)
    out = {}
    for a, sz in sizes.items():
        if sz < 24 or a < base or a - base + sz > len(data):
            continue
        toks = []
        for i in md.disasm(data[a - base:a - base + sz], a):
            ops = re.sub(r"#-?0x[0-9a-f]+|#-?\d+", "#", i.op_str)
            toks.append(i.mnemonic + " " + ops)
        if len(toks) >= 8:
            out[a] = hashlib.sha1("\n".join(toks).encode()).hexdigest()
    return out


def load(ver):
    out = os.path.join(PROJ, "out", ver)
    funcs = {}
    sizes = {}
    with open(os.path.join(out, "functions.tsv")) as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            funcs[int(p[0], 16)] = p[1]
            sizes[int(p[0], 16)] = int(p[2])
    funcs["_sizes"] = sizes
    strings = collections.defaultdict(set)
    with open(os.path.join(out, "strings.tsv"), encoding="utf-8", errors="replace") as f:
        next(f)
        for line in f:
            addr, users, text = line.rstrip("\n").split("\t", 2)
            if len(text) < 6 or not users:
                continue
            for u in users.split(","):
                strings[text].add(int(u, 16))
    callees = collections.defaultdict(list)
    with open(os.path.join(out, "calls.tsv")) as f:
        for line in f:
            a, b = line.split()
            callees[int(a, 16)].append(int(b, 16))
    return funcs, strings, callees


def named(name):
    return not name.startswith(("FUN_", "thunk_FUN_", "IRQ", "Exception"))


def carry(src, dst):
    sf, ss, sc = load(src)
    df, ds, dc = load(dst)
    pairs = {}
    for text, users in ss.items():
        if len(users) != 1 or text not in ds or len(ds[text]) != 1:
            continue
        a = next(iter(users))
        b = next(iter(ds[text]))
        if a in pairs and pairs[a] != b:
            pairs[a] = None
        else:
            pairs.setdefault(a, b)
    pairs = {a: b for a, b in pairs.items() if b is not None}
    via = {a: "string" for a in pairs}

    sh = shapes(src, sf.pop("_sizes"))
    dh = shapes(dst, df.pop("_sizes"))
    by_s = collections.defaultdict(list)
    by_d = collections.defaultdict(list)
    for a, h in sh.items():
        by_s[h].append(a)
    for b, h in dh.items():
        by_d[h].append(b)
    used = set(pairs.values())
    for h, al in by_s.items():
        bl = by_d.get(h, [])
        if len(al) == 1 and len(bl) == 1 and al[0] not in pairs and bl[0] not in used:
            pairs[al[0]] = bl[0]
            used.add(bl[0])
            via[al[0]] = "shape"

    for _ in range(3):
        used = set(pairs.values())
        added = 0
        for a, b in list(pairs.items()):
            sa = [c for c in dict.fromkeys(sc.get(a, [])) if c not in pairs]
            db = [c for c in dict.fromkeys(dc.get(b, [])) if c not in used]
            if len(sa) == 1 and len(db) == 1 and named(sf.get(sa[0], "FUN_")):
                pairs[sa[0]] = db[0]
                used.add(db[0])
                via[sa[0]] = "callee"
                added += 1
        if not added:
            break

    rows = []
    for a, b in sorted(pairs.items(), key=lambda x: x[1]):
        n = sf.get(a, "")
        if named(n):
            rows.append((b, n, via[a], a))
    path = os.path.join(ROOT, "docs", "symbols", f"{dst}.auto.csv")
    with open(path, "w") as f:
        f.write(f"address,name,comment\n# carried from {src} by tools/carry_names.py\n")
        for b, n, how, a in rows:
            f.write(f"0x{b:08X},{n},via {how} from {src} 0x{a:08X}\n")
    return rows, len(pairs)


def main():
    src, *dsts = sys.argv[1:]
    for dst in dsts:
        rows, total = carry(src, dst)
        print(f"{dst}: {total} functions paired, {len(rows)} names carried -> docs/symbols/{dst}.auto.csv")


if __name__ == "__main__":
    main()
