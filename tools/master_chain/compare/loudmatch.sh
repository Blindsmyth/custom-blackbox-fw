#!/usr/bin/env bash
# loudmatch.sh in.wav out.wav [target_lufs=-14]
# Applies one plain gain so the integrated loudness hits the target (no limiting, 32-bit float out),
# so chains can be compared at equal loudness. Prints the gain used.
set -euo pipefail
in=$1; out=$2; target=${3:--14}
I=$(ffmpeg -hide_banner -nostats -i "$in" -af ebur128=peak=true -f null - 2>&1 |
    awk '/Integrated loudness:/ {g=1} g && /I:/ {print $2; exit}')
gain=$(awk -v t="$target" -v i="$I" 'BEGIN {printf "%.2f", t - i}')
ffmpeg -hide_banner -nostats -y -i "$in" -af "volume=${gain}dB" -c:a pcm_f32le "$out" 2>/dev/null
echo "$(basename "$in"): ${I} LUFS -> ${target} LUFS (gain ${gain} dB)"
