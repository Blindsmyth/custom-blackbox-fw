#!/bin/sh
# Build bank-2 leaf ELF (RB + CMSIS) and pack BBEXTRA.BIN (flash image only).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
T=${ARM_GNU:-$HOME/.local/arm-gnu-15.3/bin}
M7="$ROOT/tools/lab/rb/m7"
RB="$ROOT/tools/lab/rb/rubberband"
CMSIS="$M7/cmsis"
OUT="$HERE/out"
mkdir -p "$OUT/obj"
B2_ID=${B2_ID:-0x52425731}

(cd "$M7" && OPT=cmsis ./build.sh) >/dev/null

CC="$T/arm-none-eabi-gcc"
CXX="$T/arm-none-eabi-g++"
CPU="-mthumb -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -O2 -ffunction-sections -fdata-sections"
DEFS="-DNO_THREADING -DNO_TIMING -DNO_THREAD_CHECKS -DNO_EXCEPTIONS -DHAVE_CMSIS_DSP -DPROCESS_SAMPLE_TYPE=float -DUSE_APPROXIMATE_ATAN2 -DUSE_FAST_PHASOR -DLACK_POSIX_MEMALIGN -DUSE_OWN_ALIGNED_MALLOC -DB2_ID=$B2_ID"

"$CXX" $CPU -std=c++17 $DEFS -I"$RB" -I"$RB/src" -I"$CMSIS/Include" -I"$HERE/rb" \
    -c -o "$OUT/obj/rb_api.o" "$HERE/rb/rb_api.cpp"
"$CC" $CPU -DB2_ID=$B2_ID -c -o "$OUT/obj/b2head.o" "$HERE/b2/b2head.c"
"$CC" $CPU -c -o "$OUT/obj/rb_sys.o" "$HERE/rb/rb_sys.c"

RSP="$OUT/obj/b2.rsp"
: > "$RSP"
for o in b2head rb_api rb_sys; do
    printf '"%s"\n' "$OUT/obj/${o}.o" >> "$RSP"
done
printf '"%s"\n' "$M7/obj/rb.o" >> "$RSP"
printf '"%s"\n' "$M7/obj/fast_trig.o" >> "$RSP"
for f in arm_rfft_fast_f32 arm_rfft_fast_init_f32 arm_cfft_f32 arm_cfft_init_f32 \
         arm_cfft_radix8_f32 arm_bitreversal arm_bitreversal2 arm_common_tables arm_const_structs; do
    printf '"%s"\n' "$M7/obj/${f}.o" >> "$RSP"
done

# Flash XIP in bank2; mutable C++/libc state in SRAM1 (zeroed by rb_init via startup note).
cat > "$OUT/bank2_only.ld" <<'EOF'
ENTRY(rb_init)
MEMORY {
  BANK2 (rx) : ORIGIN = 0x08100000, LENGTH = 0xC0000
  SRAM1 (rw) : ORIGIN = 0x30000000, LENGTH = 0x40000
}
SECTIONS {
  .b2head : { KEEP(*(.b2head)) } > BANK2
  .text : {
    *(.text.rb*) *(.text*) *(.bank2)
    *(.rodata*)
    . = ALIGN(4);
    __data_load = LOADADDR(.data);
  } > BANK2
  .data : {
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  } > SRAM1 AT > BANK2
  .bss (NOLOAD) : {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    . = ALIGN(8);
    __bss_end__ = .;
    end = .;
  } > SRAM1
  __heap_start = ALIGN(8);
  __heap_end = ORIGIN(SRAM1) + LENGTH(SRAM1);
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > BANK2
  /DISCARD/ : { *(.comment .note*) }
}
EOF

"$CXX" $CPU -nostartfiles -T "$OUT/bank2_only.ld" -Wl,--gc-sections \
    -Wl,-u,rb_init -Wl,-u,rb_mix -Wl,-u,rb_reset \
    -fno-exceptions -fno-rtti \
    -o "$OUT/bank2.elf" @"$RSP" -lstdc++ -lm -lc -lgcc
"$T/arm-none-eabi-size" "$OUT/bank2.elf"

# Flash image = bank2 PT_LOAD at 0x08100000 (includes .data LMA). pack_bbextra stamps header.
python3 "$ROOT/tools/bank2/pack_bbextra.py" "$OUT/bank2.elf" -o "$OUT/BBEXTRA.BIN" --id "$B2_ID"
echo "bank2 ready: $OUT/BBEXTRA.BIN"
