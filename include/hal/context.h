#ifndef _HAL_CONTEXT_H
#define _HAL_CONTEXT_H
#if defined(__riscv)
#include <riscv/context.h>
#elif defined(__aarch64__)
#include <arm64/context.h>
#else
#error unsupported context architecture
#endif
#ifndef __ASSEMBLER__
typedef struct Hinit Hinit;
typedef struct Htrap Htrap;
struct Hinit
{
  u64 entry, sp, arg;
};
struct Htrap
{
  u32 kind, fromuser;
  u64 code, address, syscall_nr;
};
#define HT_SYSCALL   1
#define HT_IRQ       2
#define HT_TIMER     3
#define HT_PAGEFAULT 4
#define HT_ILLEGAL   5
#define HT_OTHER     6
STATIC_ASSERT(sizeof(Hinit) == 24, hinit_size);
STATIC_ASSERT(sizeof(Htrap) == 32, htrap_size);
/* IRQs disabled; caller owns independent resident stacks and their lifetimes. */
void hal_ctx_init(Hkctx *context, u64 top, void (*entry)(void));
void hal_ctx_switch(Hkctx *from, const Hkctx *to);
void hal_trap_init(void);
void kerneltrap(Hctx *context);
void hal_user_init(Hctx *context, const Hinit *initial);
void hal_user_irq(Hctx *context, u32 enabled);
u64  hal_syscall_arg(const Hctx *context, u32 slot);
void hal_ipc_finish(Hctx *context, isize status, const u64 outputs[8]);
void hal_syscall_finish(Hctx *context, isize status, const u64 outputs[7]);
void hal_trap_decode(const Hctx *context, Htrap *trap);
/* Context is the reserved frame on the current guarded kernel stack.
   Entry discards the current C continuation; repeated returns reuse this frame. */
NORETURN void hal_user_enter(Hctx *context);
/* Called with all user integer and FP state saved, on the current kernel stack.
   IRQs remain disabled until the core chooses a safe scheduling boundary. */
NORETURN void usertrap(Hctx *context);
#endif
#endif
