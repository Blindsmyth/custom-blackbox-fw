#!/usr/bin/env python3
"""Query the Ghidra exports in ~/GhidraProjects/out/<version>/.

  decomp.py <ver> show <addr|name> [...]     print decompiled C
  decomp.py <ver> save <outdir> <addr|name> [...]  write <addr>_<name>.c files
  decomp.py <ver> callers <addr|name>
  decomp.py <ver> strings <regex>            strings and the functions that use them
  decomp.py <ver> grep <regex>               functions whose C matches
"""
import os
import re
import sys

PROJ = os.environ.get("GHIDRA_PROJ", os.path.expanduser("~/GhidraProjects"))


def load(ver):
    out = os.path.join(PROJ, "out", ver)
    funcs = {}
    with open(os.path.join(out, "all.c"), encoding="utf-8", errors="replace") as f:
        cur = None
        buf = []
        for line in f:
            m = re.match(r"// ==== FUNC (0x[0-9a-f]+) (\S+)", line)
            if m:
                if cur:
                    funcs[cur[0]] = (cur[1], "".join(buf))
                cur = (int(m.group(1), 16), m.group(2))
                buf = []
            else:
                buf.append(line)
        if cur:
            funcs[cur[0]] = (cur[1], "".join(buf))
    calls = []
    with open(os.path.join(out, "calls.tsv")) as f:
        for line in f:
            a, b = line.split()
            calls.append((int(a, 16), int(b, 16)))
    return out, funcs, calls


def resolve(funcs, key):
    try:
        a = int(key, 16) & ~1
        if a in funcs:
            return a
        best = max((x for x in funcs if x <= a), default=None)
        return best
    except ValueError:
        for a, (n, _) in funcs.items():
            if n == key:
                return a
    raise SystemExit(f"unknown function {key}")


def main():
    ver, cmd, *args = sys.argv[1:]
    out, funcs, calls = load(ver)
    if cmd == "show":
        for k in args:
            a = resolve(funcs, k)
            print(f"// 0x{a:08x} {funcs[a][0]}\n{funcs[a][1]}")
    elif cmd == "save":
        d = args[0]
        os.makedirs(d, exist_ok=True)
        for k in args[1:]:
            a = resolve(funcs, k)
            n = funcs[a][0]
            with open(os.path.join(d, f"{a:08x}_{n}.c"), "w") as f:
                f.write(f"// 0x{a:08x} {n}  (Ghidra decompile, {ver})\n{funcs[a][1]}")
    elif cmd == "callers":
        a = resolve(funcs, args[0])
        for x, y in calls:
            if y == a:
                print(f"0x{x:08x} {funcs.get(x, ('?',))[0]}")
    elif cmd == "strings":
        rx = re.compile(args[0])
        with open(os.path.join(out, "strings.tsv"), encoding="utf-8", errors="replace") as f:
            next(f)
            for line in f:
                addr, users, text = line.rstrip("\n").split("\t", 2)
                if rx.search(text):
                    print(addr, users or "-", text)
    elif cmd == "grep":
        rx = re.compile(args[0])
        for a, (n, c) in sorted(funcs.items()):
            if rx.search(c):
                print(f"0x{a:08x} {n}")


if __name__ == "__main__":
    main()
