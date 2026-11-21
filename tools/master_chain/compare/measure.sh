#!/usr/bin/env bash
# measure.sh: loudness and punch metrics for mastering comparisons. ffmpeg only.
#
#   measure.sh [-t secs] [-s start] file.wav [file2.wav ...]   -> CSV on stdout (header first)
#   measure.sh -p ...                                          -> same, aligned for reading
#
# Columns
#   LUFS_I / LRA / TP       ebur128 integrated loudness, loudness range, true peak (dBTP)
#   PLR                     TP - LUFS_I, the peak-to-loudness ratio (lower = more squashed)
#   STmax                   loudest 3 s short-term loudness
#   crest_low/mid/high      peak - RMS in dB per band (<120 Hz, 120-2k, >2k), a punch proxy
#   side_low                side minus mid RMS below 150 Hz in dB (more negative = more mono bass)
#   o63 .. o8k              RMS per octave band in dB, for spectral balance deltas
#
# Compare chains only after loudness matching (loudmatch.sh); a louder chain always sounds punchier.
set -euo pipefail

T=(); PRETTY=0
while getopts "t:s:p" o; do
    case $o in
        t) T+=(-t "$OPTARG") ;;
        s) T+=(-ss "$OPTARG") ;;
        p) PRETTY=1 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { echo "usage: $0 [-t secs] [-s start] [-p] file.wav ..." >&2; exit 2; }

ff() { ffmpeg -hide_banner -nostats "${T[@]+"${T[@]}"}" -i "$1" "${@:2}" -f null - 2>&1; }

# peak and rms (dB) of a filter chain
pr() {
    ff "$1" -af "$2,astats=measure_perchannel=none:measure_overall=Peak_level+RMS_level" |
        awk '/Peak level dB/ {p=$NF} /RMS level dB/ {r=$NF} END {printf "%s %s", p, r}'
}
crest() { read -r p r <<<"$(pr "$1" "$2")"; awk -v p="$p" -v r="$r" 'BEGIN {printf "%.1f", p - r}'; }
rms() { read -r _ r <<<"$(pr "$1" "$2")"; awk -v r="$r" 'BEGIN {printf "%.1f", r}'; }

row() {
    local f=$1 log
    log=$(ff "$f" -af ebur128=peak=true)
    local I LRA TP ST
    I=$(echo "$log" | awk '/Integrated loudness:/ {g=1} g && /I:/ {print $2; exit}')
    LRA=$(echo "$log" | awk '/Loudness range:/ {g=1} g && /LRA:/ {print $2; exit}')
    TP=$(echo "$log" | awk '/True peak:/ {g=1} g && /Peak:/ {print $2; exit}')
    ST=$(echo "$log" | awk '/ S: / { for (i = 1; i <= NF; i++) if ($i == "S:") { v = $(i+1); if (v != "-inf" && (m == "" || v + 0 > m + 0)) m = v } } END {print m}')
    local PLR
    PLR=$(awk -v tp="$TP" -v i="$I" 'BEGIN {printf "%.1f", tp - i}')

    local cl cm ch sl
    cl=$(crest "$f" "lowpass=f=120")
    cm=$(crest "$f" "highpass=f=120,lowpass=f=2000")
    ch=$(crest "$f" "highpass=f=2000")
    local rm sd
    rm=$(rms "$f" "lowpass=f=150,pan=mono|c0=0.5*c0+0.5*c1")
    sd=$(rms "$f" "lowpass=f=150,pan=mono|c0=0.5*c0-0.5*c1")
    sl=$(awk -v s="$sd" -v m="$rm" 'BEGIN {printf "%.1f", s - m}')

    local oct="" fc
    for fc in 63 125 250 500 1000 2000 4000 8000; do
        oct+=",$(rms "$f" "bandpass=f=$fc:width_type=o:w=1")"
    done
    echo "$(basename "$f"),$I,$LRA,$TP,$PLR,$ST,$cl,$cm,$ch,$sl$oct"
}

out() {
    echo "file,LUFS_I,LRA,TP,PLR,STmax,crest_low,crest_mid,crest_high,side_low,o63,o125,o250,o500,o1k,o2k,o4k,o8k"
    for f in "$@"; do row "$f"; done
}

if [ "$PRETTY" = 1 ]; then out "$@" | column -s, -t; else out "$@"; fi
