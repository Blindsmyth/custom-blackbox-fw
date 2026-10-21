#!/bin/sh
# Build tools/lab/rb.wasm: Rubber Band Library (GPL-2.0-or-later) with Mixxx's
# settings, for A/B in the lab. Needs Emscripten (brew install emscripten) and
# a Rubber Band v4.0.0 checkout in tools/lab/rb/rubberband (or RUBBERBAND=/path).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
# Default: the v4.0.0 checkout next to this script (git clone --depth 1 --branch v4.0.0 https://github.com/breakfastquay/rubberband)
RB=${RUBBERBAND:-$HERE/rubberband}
em++ -O3 -std=c++17 -DNO_THREADING -DUSE_BUILTIN_FFT -DNO_TIMING -DNO_THREAD_CHECKS \
    -I "$RB" "$HERE/rb.cpp" "$RB/single/RubberBandSingle.cpp" \
    -sSTANDALONE_WASM -sALLOW_MEMORY_GROWTH -sFILESYSTEM=0 -sINITIAL_MEMORY=33554432 \
    --no-entry -o "$HERE/../rb.wasm"
echo "wrote $HERE/../rb.wasm ($(wc -c < "$HERE/../rb.wasm") bytes)"
