OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS {
  text   PT_LOAD FLAGS(5);    /* R-X */
  rodata PT_LOAD FLAGS(4);    /* R-- */
  data   PT_LOAD FLAGS(6);    /* RW- */
  bss    PT_LOAD FLAGS(6);    /* RW- */
}

SECTIONS {
  /* QEMU -kernel loads our kernel onto 0x80000000 */
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(8);
  } :text

  .rodata : {
		*(.rodata .rodata.*)
    . = ALIGN(8);
	} :rodata

  .data : {
    *(.data .data.*)
    . = ALIGN(8);
  } :data

  .bss : {
    *(.bss .bss.*)
    . = ALIGN(8);
  } :bss

  /* Ensures the kernel is page aligned */
  . = ALIGN(0x1000);
  _end = .;
}
