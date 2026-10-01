#!/bin/bash
# Headless Ghidra import/analysis of one firmware version.
#
#   tools/ghidra_import.sh <version> [import|post] [nodecomp]
#
# import (default): import firmware/bins/<version>/BLACKBOX.BIN at 0x08040000 as Cortex-M
#                   Thumb, run SetupCortexM, full auto-analysis, then the post scripts.
# post:             re-run ApplySymbols + ExportAll on the existing program (no re-analysis).
#
# The Ghidra project lives outside Dropbox in $GHIDRA_PROJ (default ~/GhidraProjects).
# Exports go to $GHIDRA_PROJ/out/<version>/ (functions.tsv, strings.tsv, calls.tsv, all.c).
# Names come from docs/symbols/<version>.csv when present.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:?version, e.g. 3.1.9}"
MODE="${2:-import}"
DECOMP="${3:-}"

export JAVA_HOME="${JAVA_HOME_GHIDRA:-/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home}"
HEADLESS=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
PROJ="${GHIDRA_PROJ:-$HOME/GhidraProjects}"
OUT="$PROJ/out/$VER"
SYMS="$ROOT/docs/symbols/$VER.csv"
[[ -f "$SYMS" ]] || SYMS="$ROOT/docs/symbols/$VER.auto.csv"
mkdir -p "$PROJ" "$OUT"

BIN=$(ls "$ROOT"/firmware/bins/"$VER"/BLACKBOX.* 2>/dev/null | head -1 || true)
if [[ -z "$BIN" && "$VER" == gamechanger* ]]; then
  BIN=$(ls "$ROOT"/firmware/gamechanger/*/BLACKBOX.* | head -1)
fi
[[ -n "$BIN" ]] || { echo "no BLACKBOX.BIN for $VER" >&2; exit 1; }

POST=(-scriptPath "$ROOT/tools/ghidra"
      -postScript ApplySymbols.java "$SYMS"
      -postScript ExportAll.java "$OUT" $DECOMP)

if [[ "$MODE" == import ]]; then
  "$HEADLESS" "$PROJ" "blackbox/$VER" -overwrite \
    -import "$BIN" \
    -processor ARM:LE:32:Cortex -cspec default \
    -loader BinaryLoader -loader-baseAddr 0x08040000 \
    -preScript SetupCortexM.java \
    -analysisTimeoutPerFile 3600 \
    "${POST[@]}" 2>&1 | grep -vE '^\s*INFO  (Analy|Pack|Class|Using|Found|Process|User|Head|Ghidra|Load|Creat|Import|REPORT|  )' || true
else
  "$HEADLESS" "$PROJ" "blackbox/$VER" -process "$(basename "$BIN")" -noanalysis \
    "${POST[@]}" 2>&1 | grep -E 'ERROR|Apply|Exception' || true
fi
echo "exports: $OUT"
