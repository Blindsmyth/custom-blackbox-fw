#!/bin/sh
# Check out Bungee v2.4.35 (MPL-2.0) with its Eigen submodule into tools/stretch/bungee/bungee and apply
# the Blackbox change (bungee-blackbox.patch):
#   - BUNGEE_FIXED_PITCH: no input/output resampling buffers (pitch 1, 48 kHz in and out; 65 KB), and
#     partial lists sized to the bins (48 KB)
# The FFT back-end is Fourier_cmsis.cpp next to this script (CMSIS-DSP, built instead of src/Fourier.cpp).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEST="$HERE/bungee"
if [ ! -d "$DEST/.git" ]; then
    git clone -q --depth 1 --branch v2.4.35 https://github.com/bungee-audio-stretch/bungee "$DEST"
    git -C "$DEST" submodule update -q --init --depth 1 submodules/eigen
fi
cd "$DEST"
if git apply --check "$HERE/bungee-blackbox.patch" 2>/dev/null; then
    git apply "$HERE/bungee-blackbox.patch"
    echo "patched $DEST"
elif git apply --reverse --check "$HERE/bungee-blackbox.patch" 2>/dev/null; then
    echo "$DEST already patched"
else
    echo "bungee-blackbox.patch does not apply to $DEST" >&2
    exit 1
fi
