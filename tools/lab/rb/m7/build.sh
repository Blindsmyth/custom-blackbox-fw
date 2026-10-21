#!/bin/sh
# Cross-compile the Rubber Band cost probe for the Blackbox's Cortex-M7 (STM32H743).
# Needs the Arm GNU Toolchain with newlib/libstdc++ (ARM_GNU=/path/to/its bin), and the
# Rubber Band checkout in tools/lab/rb/rubberband.
#
#   ./build.sh              → probe.elf     (double, builtin FFT, DP FPU)
#   FPU=sp ./build.sh       → probe_sp.elf  (double soft-float on SP FPU)
#   PREC=f32 ./build.sh     → probe_f32.elf (float process, builtin FFT)
#   OPT=cmsis ./build.sh    → probe_cmsis.elf (float + CMSIS-DSP FFT + fast trig)
#   OPT=trig ./build.sh     → probe_trig.elf  (float + builtin FFT + fast trig)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
T=${ARM_GNU:-$HOME/.local/arm-gnu-15.3/bin}
RB=${RUBBERBAND:-$HERE/../rubberband}
CMSIS="$HERE/cmsis"
CC="$T/arm-none-eabi-gcc"
CXX="$T/arm-none-eabi-g++"
CPUFLAGS="-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -O2 -ffunction-sections -fdata-sections"
COMMON_DEFS="-DNO_THREADING -DNO_TIMING -DNO_THREAD_CHECKS -DLACK_POSIX_MEMALIGN -DUSE_OWN_ALIGNED_MALLOC"
OBJDIR="$HERE/obj"
mkdir -p "$OBJDIR"
RSP="$OBJDIR/link.rsp"
: > "$RSP"

cmsis_obj() {
    src=$1
    base=$(basename "$src" .c)
    o="$OBJDIR/${base}.o"
    "$CC" $CPUFLAGS "$FPUFLAG" -DARM_MATH_CM7 -D__FPU_PRESENT=1 -I"$CMSIS/Include" \
        -c -o "$o" "$src"
    printf '"%s"\n' "$o" >> "$RSP"
}

if [ "$OPT" = cmsis ]; then
    OUT=probe_cmsis.elf
    FPUFLAG=-mfpu=fpv5-sp-d16
    RB_SRC="$HERE/RubberBandOpt.cpp"
    RB_DEFS="$COMMON_DEFS -DHAVE_CMSIS_DSP -DNO_EXCEPTIONS -DPROCESS_SAMPLE_TYPE=float -DUSE_APPROXIMATE_ATAN2 -DUSE_FAST_PHASOR"
    set -- -I"$RB" -I"$RB/src" -I"$CMSIS/Include"
    for f in arm_rfft_fast_f32.c arm_rfft_fast_init_f32.c arm_cfft_f32.c arm_cfft_init_f32.c \
             arm_cfft_radix8_f32.c arm_bitreversal.c arm_bitreversal2.c; do
        cmsis_obj "$CMSIS/Source/TransformFunctions/$f"
    done
    for f in arm_common_tables.c arm_const_structs.c; do
        cmsis_obj "$CMSIS/Source/CommonTables/$f"
    done
elif [ "$OPT" = trig ]; then
    OUT=probe_trig.elf
    FPUFLAG=-mfpu=fpv5-sp-d16
    RB_SRC="$HERE/RubberBandTrig.cpp"
    RB_DEFS="$COMMON_DEFS -DUSE_BUILTIN_FFT -DNO_EXCEPTIONS -DPROCESS_SAMPLE_TYPE=float -DUSE_APPROXIMATE_ATAN2 -DUSE_FAST_PHASOR"
    set -- -I"$RB" -I"$RB/src"
elif [ "$PREC" = f32 ]; then
    OUT=probe_f32.elf
    FPUFLAG=-mfpu=fpv5-sp-d16
    RB_SRC="$RB/single/RubberBandSingle.cpp"
    RB_DEFS="$COMMON_DEFS -DUSE_BUILTIN_FFT -DLACK_SINCOS -DPROCESS_SAMPLE_TYPE=float"
    set -- -I"$RB"
elif [ "$FPU" = sp ]; then
    OUT=probe_sp.elf
    FPUFLAG=-mfpu=fpv5-sp-d16
    RB_SRC="$RB/single/RubberBandSingle.cpp"
    RB_DEFS="$COMMON_DEFS -DUSE_BUILTIN_FFT -DLACK_SINCOS"
    set -- -I"$RB"
else
    OUT=probe.elf
    FPUFLAG=-mfpu=fpv5-d16
    RB_SRC="$RB/single/RubberBandSingle.cpp"
    RB_DEFS="$COMMON_DEFS -DUSE_BUILTIN_FFT -DLACK_SINCOS"
    set -- -I"$RB"
fi

"$CXX" $CPUFLAGS "$FPUFLAG" -std=c++17 $RB_DEFS "$@" \
    -c -o "$OBJDIR/rb.o" "$RB_SRC"
printf '"%s"\n' "$OBJDIR/rb.o" >> "$RSP"

if [ "$OPT" = cmsis ] || [ "$OPT" = trig ]; then
    "$CXX" $CPUFLAGS "$FPUFLAG" -std=c++17 $RB_DEFS "$@" \
        -c -o "$OBJDIR/fast_trig.o" "$HERE/fast_trig.cpp"
    printf '"%s"\n' "$OBJDIR/fast_trig.o" >> "$RSP"
fi

"$CXX" $CPUFLAGS "$FPUFLAG" -std=c++17 $RB_DEFS -I"$RB" -I"$RB/src" \
    -c -o "$OBJDIR/rbm7.o" "$HERE/rbm7.cpp"
printf '"%s"\n' "$OBJDIR/rbm7.o" >> "$RSP"
"$CC" $CPUFLAGS "$FPUFLAG" -c -o "$OBJDIR/sys.o" "$HERE/sys.c"
printf '"%s"\n' "$OBJDIR/sys.o" >> "$RSP"

"$CXX" $CPUFLAGS "$FPUFLAG" -nostartfiles -T "$HERE/probe.ld" -Wl,--gc-sections \
    -Wl,-u,probe_boot -Wl,-u,probe_setup -Wl,-u,probe_block -Wl,-u,probe_out -Wl,-u,probe_heap_used \
    -o "$HERE/$OUT" @"$RSP" -lstdc++ -lm -lc -lgcc
"$T/arm-none-eabi-size" "$HERE/$OUT"
