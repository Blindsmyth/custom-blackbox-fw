#!/bin/sh
# Build tools/lab/bungee.wasm: Bungee Basic (MPL-2.0), a phase-vocoder
# Blackbox candidate, for A/B against Rubber Band in the lab. Needs Emscripten
# (brew install emscripten) and a Bungee checkout with submodules in
# tools/lab/bungee/bungee (or BUNGEE=/path):
#   git clone --recursive --depth 1 --shallow-submodules --branch v2.4.35 \
#       https://github.com/bungee-audio-stretch/bungee tools/lab/bungee/bungee
# PFFFT is built scalar (PFFFT_SIMD_DISABLE) so wasm time is closer to the M7.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BG=${BUNGEE:-$HERE/bungee}
OBJ=$(mktemp -d)
emcc -O3 -ffast-math -fno-finite-math-only -DPFFFT_SIMD_DISABLE \
    -c -o "$OBJ/pffft.o" "$BG/submodules/pffft/pffft.c"
em++ -O3 -std=c++20 -fwrapv -fno-exceptions \
    -DBUNGEE_VISIBILITY= -DBUNGEE_SELF_TEST=0 -Deigen_assert=BUNGEE_ASSERT1 -DEIGEN_DONT_PARALLELIZE=1 \
    '-DBUNGEE_VERSION="lab"' \
    -I "$BG" -I "$BG/submodules/eigen" -I "$BG/submodules/pffft" \
    "$HERE/bungee.cpp" "$BG"/src/*.cpp "$OBJ/pffft.o" \
    -sSTANDALONE_WASM -sALLOW_MEMORY_GROWTH -sFILESYSTEM=0 -sINITIAL_MEMORY=33554432 \
    --no-entry -o "$HERE/../bungee.wasm"
rm -rf "$OBJ"
echo "wrote $HERE/../bungee.wasm ($(wc -c < "$HERE/../bungee.wasm") bytes)"
