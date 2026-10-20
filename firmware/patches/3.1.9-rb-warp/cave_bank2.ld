/* Dual-region link: bank-1 cave + bank-2 leaf (RB + CMSIS). Never map sector 7. */
MEMORY {
  CAVE  (rx) : ORIGIN = 0x080F1E80, LENGTH = 0xE180
  BANK2 (rx) : ORIGIN = 0x08100000, LENGTH = 0xC0000
}
SECTIONS {
  .b2head : { KEEP(*(.b2head)) } > BANK2
  .bank2 : {
    *(.bank2 .bank2.*)
    *rb_api.o(.text* .rodata*)
    *RubberBand* (.text* .rodata*)
    *fast_trig* (.text* .rodata*)
    *arm_* (.text* .rodata*)
  } > BANK2
  .cave : {
    *(.text .text.* .rodata .rodata.*)
  } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment .note*) }
}
