OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5);  /* R-X */
  rodata PT_LOAD FLAGS(4);  /* R-- */
  data   PT_LOAD FLAGS(6);  /* RW- */
}

SECTIONS
{
  . = 0;
  .text : { *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) } :rodata
  .data : { *(.data .data.*) *(.sdata .sdata.*) *(.srodata .srodata.*) } :data
  .got : { *(.got .got.plt) } :data
  .dynamic : { *(.dynamic) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.interp) *(.note.*) *(.eh_frame*) *(.gnu.hash) *(.dynsym) *(.dynstr) *(.rela.*) }
}
