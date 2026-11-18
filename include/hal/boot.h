#ifndef _HAL_BOOT_H
#define _HAL_BOOT_H
#include <types.h>
#include <dtb.h>
int  hal_debug_init(const Dtuart *uart);
void hal_debug_putc(u8 c);
void hal_debug_rebase(u64 address);
/* IRQs masked. Handoff is one-shot/idempotent after physical TX drain;
   BUSY does not transfer ownership. Panic is fatal-only, never resumes a peer. */
int           hal_debug_handoff(void);
int           hal_debug_panic(void);
u64           hal_debug_lost(void);
void          hal_debug_sync(void);
u64           hal_boot_machine(void);
NORETURN void hal_halt(void);
#endif
