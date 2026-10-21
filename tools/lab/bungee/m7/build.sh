#!/bin/sh
# Cross-compile the Bungee Basic cost probe for the Blackbox's Cortex-M7 (STM32H743),
# reusing the Rubber Band harness in tools/lab/rb/m7 (probe.ld, sys.c, measure.py, cmsis).
# pffft is swapped for CMSIS-DSP arm_rfft_fast_f32 (Fourier_cmsis.cpp replaces src/Fourier.cpp).
# Needs the Arm GNU Toolchain (ARM_GNU=/path/to/its bin) and the Bungee checkout with
# submodules in tools/lab/bungee/bungee.
#
#   ./build.sh          → probe_bungee.elf     (default 512-frame synthesis hop)
#   HOP=-1 ./build.sh   → probe_bungee_h256.elf (log2SynthesisHopAdjust -1: 256-frame hop)
#   FFT=pffft ./build.sh → probe_bungee_pffft.elf (stock scalar pffft backend, reference)
#   SPLIT=1 ./build.sh  → probe_bungee_split.elf (analyse/synthesise in alternate blocks)
#   BLOCKS=40 RATIOS=0.8,0.6,1.25 LABEL=bungee \
#     ../../rb/m7/.venv/bin/python ../../rb/m7/measure.py probe_bungee.elf ../../rb/m7/test_loop.wav 0
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
T=${ARM_GNU:-$HOME/.local/arm-gnu-15.3/bin}
BG=${BUNGEE:-$HERE/../bungee}
RBM7="$HERE/../../rb/m7"
CMSIS="$RBM7/cmsis"
CC="$T/arm-none-eabi-gcc"
CXX="$T/arm-none-eabi-g++"
CPUFLAGS="-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -O2 -ffunction-sections -fdata-sections"
CXXFLAGS="-std=c++20 -fwrapv -fno-exceptions -fno-rtti -DBUNGEE_VISIBILITY= -DBUNGEE_SELF_TEST=0 \
  -Deigen_assert=BUNGEE_ASSERT1 -DEIGEN_DONT_PARALLELIZE=1 -DEIGEN_NO_IO"
HOP=${HOP:-0}
OUT=probe_bungee.elf
[ "$HOP" = 0 ] || OUT=probe_bungee_h$((512 >> -HOP)).elf
[ "$FFT" = pffft ] && OUT=${OUT%.elf}_pffft.elf
SPLIT=${SPLIT:-0}
[ "$SPLIT" = 1 ] && OUT=${OUT%.elf}_split.elf
OBJDIR="$HERE/obj"
mkdir -p "$OBJDIR"
RSP="$OBJDIR/link.rsp"
: > "$RSP"

for f in arm_rfft_fast_f32.c arm_rfft_fast_init_f32.c arm_cfft_f32.c arm_cfft_init_f32.c \
         arm_cfft_radix8_f32.c arm_bitreversal.c arm_bitreversal2.c; do
    set -- "$@" "$CMSIS/Source/TransformFunctions/$f"
done
for f in arm_common_tables.c arm_const_structs.c; do
    set -- "$@" "$CMSIS/Source/CommonTables/$f"
done
for src in "$@"; do
    o="$OBJDIR/$(basename "$src" .c).o"
    "$CC" $CPUFLAGS -DARM_MATH_CM7 -D__FPU_PRESENT=1 -I"$CMSIS/Include" -c -o "$o" "$src"
    printf '"%s"\n' "$o" >> "$RSP"
done

if [ "$FFT" = pffft ]; then
    "$CC" $CPUFLAGS -ffast-math -fno-finite-math-only -DPFFFT_SIMD_DISABLE -c -o "$OBJDIR/pffft.o" \
        "$BG/submodules/pffft/pffft.c"
    printf '"%s"\n' "$OBJDIR/pffft.o" >> "$RSP"
    FOURIER=Fourier.cpp
else
    FOURIER=Fourier_cmsis.cpp
fi
for src in "$BG"/src/*.cpp "$HERE/Fourier_cmsis.cpp" "$HERE/bgm7.cpp"; do
    case $(basename "$src") in Fourier.cpp|Fourier_cmsis.cpp) [ "$(basename "$src")" = $FOURIER ] || continue;; esac
    o="$OBJDIR/bg_$(basename "$src" .cpp).o"
    "$CXX" $CPUFLAGS $CXXFLAGS -DBUNGEE_VERSION='"m7"' -DBG_HOP_ADJUST=$HOP -DBG_SPLIT=$SPLIT -I"$BG" -I"$BG/submodules/eigen" -I"$CMSIS/Include" -I"$BG/submodules/pffft" \
        -c -o "$o" "$src"
    printf '"%s"\n' "$o" >> "$RSP"
done
"$CC" $CPUFLAGS -c -o "$OBJDIR/sys.o" "$RBM7/sys.c"
printf '"%s"\n' "$OBJDIR/sys.o" >> "$RSP"

"$CXX" $CPUFLAGS -nostartfiles -T "$RBM7/probe.ld" -Wl,--gc-sections \
    -Wl,-u,probe_boot -Wl,-u,probe_setup -Wl,-u,probe_block -Wl,-u,probe_out -Wl,-u,probe_heap_used \
    -o "$HERE/$OUT" @"$RSP" -lstdc++ -lm -lc -lgcc
"$T/arm-none-eabi-size" "$HERE/$OUT"
