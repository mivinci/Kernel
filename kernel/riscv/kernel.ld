ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x80200000;
 __kernel_start = .;
 .text : { __text_start = .; KEEP(*(.text.boot)) *(.text .text.*) . = ALIGN(4096); __text_end = .; } :text
 .rodata : { __rodata_start = .; *(.rodata .rodata.*) . = ALIGN(4096); __rodata_end = .; } :rodata
 . = ALIGN(4096);
 .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
 .bss (NOLOAD) : {
  . = ALIGN(16); __bss_start = .;
  *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
  . = ALIGN(16); __bss_end = .;
 } :data
 . = ALIGN(4096);
 .stacks (NOLOAD) : {
  __stack_guard = .; . += 4096; __stack_bottom = .; . += 16384; __stack_top = .;
  __emergency_guard = .; . += 4096; __emergency_bottom = .; . += 4096; __emergency_top = .;
 } :data
 __kernel_end = .;
 /DISCARD/ : { *(.eh_frame*) *(.comment) }
 ASSERT(__kernel_end - __kernel_start < 0x1000000, "bootstrap exceeds reserved window")
 ASSERT((__stack_top & 15) == 0, "stack alignment")
}
