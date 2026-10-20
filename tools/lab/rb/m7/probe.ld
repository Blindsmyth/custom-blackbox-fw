/* Emulated memory map for the probe (not the Blackbox's): code in flash, data and heap in a large RAM. */
ENTRY(probe_boot)
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x24000000, LENGTH = 16M
}
SECTIONS {
  .text : { KEEP(*(.text.probe*)) *(.text*) *(.rodata*) . = ALIGN(4);
            __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
            __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) __init_array_end = .;
            __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
            KEEP(*(.init)) KEEP(*(.fini)) } > FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .data : { __data_load = LOADADDR(.data); __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; end = .; } > RAM
  __heap_start = ALIGN(8);
  __heap_end = ORIGIN(RAM) + LENGTH(RAM) - 0x10000;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
