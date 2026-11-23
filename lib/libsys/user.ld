ENTRY(_start)
PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
  tls PT_TLS FLAGS(4);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS
{
  . = 0x10000;
  .text : { *(.text._start) *(.text .text.*) } :text
  . = ALIGN(4096);
  .rodata : {
    *(.rodata .rodata.*)
    . = ALIGN(8);
    PROVIDE_HIDDEN(__native_tls = .);
    QUAD(ADDR(.tdata))
    QUAD(SIZEOF(.tdata))
    QUAD(SIZEOF(.tbss) ? ADDR(.tbss) + SIZEOF(.tbss) - ADDR(.tdata) : SIZEOF(.tdata))
    QUAD(MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)))
  } :rodata
  .preinit_array ALIGN(8) : {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } :rodata
  .init_array ALIGN(8) : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } :rodata
  .fini_array ALIGN(8) : {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } :rodata
  . = ALIGN(4096);
  .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
  .tdata ALIGN(MAX(ALIGNOF(.tdata), ALIGNOF(.tbss))) : { *(.tdata .tdata.*) } :data :tls
  .tbss (NOLOAD) : { *(.tbss .tbss.*) *(.tcommon) } :data :tls
  .bss (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :data
  PROVIDE_HIDDEN(__image_end = ALIGN(4096));
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
