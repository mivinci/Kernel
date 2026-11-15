/* Physical bootstrap entry; not a userspace or permanent HAL ABI. */
#ifndef _BOOT_H
#define _BOOT_H
#include <types.h>
void        main(u64 cpu, u64 dtb, u64 mode, u64 mask, u64 vm);
extern char __kernel_start[], __kernel_end[], __stack_bottom[], __stack_top[];
extern char __bss_start[], __bss_end[], __emergency_bottom[], __emergency_top[];
extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern char __stack_guard[], __emergency_guard[];
#endif
