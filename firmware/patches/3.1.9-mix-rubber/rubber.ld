/* 3.1.R: one link, two flash banks.
 *
 * CAVE  - bank 1 after the stock image; the SD installer refuses an image past 0x08100000.
 *         Only the cave asm and tools/bank2 go here: bank 1 must run without bank 2.
 * BANK2 - flash bank 2 sectors 0-5 (0x08100000, 768 KB), programmed from BBRUBBER.BIN by the card
 *         loader. Sector 6 holds the loader's claim word, sector 7 is factory calibration: neither
 *         is in this region.
 * B2RAM - SRAM1 (0x30000000, 288 KB with SRAM2/3; no stock use, docs/map-319.md): bank 2's .data
 *         and .bss. rubber_boot copies .data from its bank 2 load address and zeroes .bss.
 * Bank 1 reaches bank 2 only through the header's function pointers (rubber.h RbHeader);
 * build_patch.py rejects any direct branch from the cave into bank 2. */
MEMORY {
  CAVE  (rx)  : ORIGIN = 0x080F1E80, LENGTH = 0x08100000 - 0x080F1E80
  BANK2 (rx)  : ORIGIN = 0x08100000, LENGTH = 0x000C0000
  B2RAM (rwx) : ORIGIN = 0x30000000, LENGTH = 0x00048000
}
ENTRY(hook_register)
SECTIONS {
  .cave : {
    KEEP(*rubber_cave.o(.text .text.*))
    *rubber_cave.o(.rodata .rodata.*)
    KEEP(*b2pair.o(.text .text.* .rodata .rodata.*))
    KEEP(*b2load.o(.text .text.* .rodata .rodata.*))
  } > CAVE

  .b2head : { KEEP(*(.b2head)) } > BANK2
  .b2text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    __b2_init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    __b2_init_array_end = .;
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    __preinit_array_start = .; __preinit_array_end = .;
    __init_array_start = .; __init_array_end = .;
    __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
  } > BANK2
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > BANK2
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > BANK2

  .b2data : {
    . = ALIGN(4);
    __b2_data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __b2_data_end = .;
  } > B2RAM AT > BANK2
  __b2_data_load = LOADADDR(.b2data);
  __b2_load_end = LOADADDR(.b2data) + SIZEOF(.b2data);

  .b2bss (NOLOAD) : {
    . = ALIGN(4);
    __b2_bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __b2_bss_end = .;
    end = .;
  } > B2RAM

  /DISCARD/ : { *(.comment) *(.note*) }
}
