#ifndef _HAL_BOOT_H
#define _HAL_BOOT_H
#include <types.h>
void          hal_debug_putc(u8 c);
void          hal_debug_rebase(u64 address);
u64           hal_boot_machine(void);
NORETURN void hal_halt(void);
#endif
