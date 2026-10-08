ENTRY(hook_register)
SECTIONS
{
  . = 0x080F1E80;
  .text : {
    *(.text*)
    *(.rodata*)
    *(.rodata.*)
  }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
