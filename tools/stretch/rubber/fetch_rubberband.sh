#!/bin/sh
# Check out Rubber Band v4.0.0 (GPL-2.0-or-later) into tools/stretch/rubber/rubberband and apply the
# Blackbox changes (rubberband-blackbox.patch):
#   - a CMSIS-DSP float FFT back-end (HAVE_CMSIS_DSP, fft_cmsis_impl.inc next to this script)
#   - USE_FAST_PHASOR: table sin/cos for inversePolar; approximate_atan2f returns its value
#   - RB_FIXED_PITCH: R2 real-time mode keeps no resampler and one window size (7.1 MB -> ~0.5 MB heap)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEST="$HERE/rubberband"
if [ ! -d "$DEST/.git" ]; then
    git clone -q --depth 1 --branch v4.0.0 https://github.com/breakfastquay/rubberband "$DEST"
fi
cd "$DEST"
if git apply --check "$HERE/rubberband-blackbox.patch" 2>/dev/null; then
    git apply "$HERE/rubberband-blackbox.patch"
    echo "patched $DEST"
elif git apply --reverse --check "$HERE/rubberband-blackbox.patch" 2>/dev/null; then
    echo "$DEST already patched"
else
    echo "rubberband-blackbox.patch does not apply to $DEST" >&2
    exit 1
fi
