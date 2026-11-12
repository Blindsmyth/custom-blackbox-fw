#!/bin/sh
# Cross-compile the master-chain cost probe for the Blackbox Cortex-M7 with the bank-2 flags
# (-O2, hard float). The chain is float-only; FPU defaults to fpv5-sp-d16 because Unicorn
# models no double-precision FPU (FPU=dp builds the bank-2 fpv5-d16 variant). Reuses the Rubber Band probe's linker script and syscall stubs.
#   tools/master_chain/m7/build.sh   -> probe.elf
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
T=${ARM_GNU:-$HOME/.local/arm-gnu-15.3/bin}
RB="$HERE/../../lab/rb/m7"
DSP="$HERE/../src/dsp"
FPUFLAG=-mfpu=fpv5-sp-d16; [ "$FPU" = dp ] && FPUFLAG=-mfpu=fpv5-d16
FLAGS="-mthumb -mcpu=cortex-m7 -mfloat-abi=hard $FPUFLAG -O2 -ffunction-sections -fdata-sections"
mkdir -p "$HERE/obj"
for f in mc_dsp mc_clr mc_cmp mc_max; do
    "$T/arm-none-eabi-gcc" $FLAGS -std=c11 -I"$DSP" -c -o "$HERE/obj/$f.o" "$DSP/$f.c"
done
"$T/arm-none-eabi-gcc" $FLAGS -std=c11 -I"$DSP" -c -o "$HERE/obj/probe.o" "$HERE/probe.c"
"$T/arm-none-eabi-gcc" $FLAGS -c -o "$HERE/obj/sys.o" "$RB/sys.c"
"$T/arm-none-eabi-gcc" $FLAGS -nostartfiles -T "$RB/probe.ld" -Wl,--gc-sections \
    -Wl,-u,probe_boot -Wl,-u,probe_setup -Wl,-u,probe_block -Wl,-u,probe_sizes \
    -o "$HERE/probe.elf" "$HERE"/obj/*.o -lm -lc -lgcc
"$T/arm-none-eabi-size" "$HERE"/obj/mc_*.o "$HERE/probe.elf"
