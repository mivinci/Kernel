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
#define HT_SYSCALL      1
#define HT_IRQ          2
#define HT_TIMER        3
#define HT_PAGEFAULT    4
#define HT_ILLEGAL      5
#define HT_OTHER        6
#define HT_ACCESS       7
#define HT_ALIGN        8
#define HT_BUS          9
#define HT_ARITH        10
#define HAL_CONTEXT_BAD -1
STATIC_ASSERT(sizeof(Hinit) == 24, hinit_size);
STATIC_ASSERT(sizeof(Htrap) == 32, htrap_size);
/* IRQs disabled; caller owns independent resident stacks and their lifetimes. */
void hal_ctx_init(Hkctx *context, u64 top, void (*entry)(void));
void hal_ctx_switch(Hkctx *from, const Hkctx *to);
void hal_trap_init(void);
void kerneltrap(Hctx *context);
void hal_user_init(Hctx *context, const Hinit *initial);
void hal_user_irq(Hctx *context, u32 enabled);
u64  hal_user_sp(const Hctx *context);
/* Register-only signal transition. Caller separately owns/validates the full
   writable frame, executable mappings, top-frame cookie and PID/image. */
int hal_signal_enter(Hctx *context, u64 frame, u64 trampoline, u64 handler, u64 flags, u32 signum);
/* No writes on rejection. GPR/FP and user condition flags can be edited;
   privilege/IRQ/diagnostic fields are tied to the trusted layer snapshot.
   out may alias either complete input, but arbitrary partial overlap is not supported. */
int  hal_signal_restore(Hctx *out, const Hctx *trusted, const Hctx *candidate);
u64  hal_syscall_arg(const Hctx *context, u32 slot);
void hal_ipc_finish(Hctx *context, isize status, const u64 outputs[8]);
void hal_syscall_finish(Hctx *context, isize status, const u64 outputs[7]);
void hal_syscall_result(Hctx *context, isize status, const u64 outputs[7]);
/* Derive a disjoint child user frame from a fully saved raw forkprep trap.
   Child returns status=0, first result=0, PID in second, Fix in third; other
   outputs/image metadata are zero. RV advances PC once; ARM retains ELR.
   Validates only register/encoding boundaries, never PID/Fix authority or
   mapped permissions. fixva=0 is allowed for an unpublished preparation.
   Parent and output stay unchanged on error; buffers must be disjoint. */
int  hal_user_fork(Hctx *out, const Hctx *parent, u64 pid, u64 fixva);
void hal_user_forkfix(Hctx *child, u64 fixva);
void hal_trap_decode(const Hctx *context, Htrap *trap);
/* Normalized abi/signal.h FAULT reason. PAGEFAULT initially returns MAPERR;
   the core distinguishes a present denied mapping after COW handling.
   Register-only; raw diagnostic PC/address/cause remain unchanged. */
u32 hal_fault_reason(const Hctx *context);
/* Siginfo address: PC for instruction/arithmetic/unknown exceptions, saved
   SP for SP alignment, zero if the hardware says its fault address is invalid.
   The raw Htrap/Life diagnostic address is unaffected. */
u64 hal_fault_address(const Hctx *context);
/* Ordinary lower-privilege store permission/page fault eligible for COW.
   Core still verifies logical mapping, marker and ownership. No mutation. */
u32 hal_user_writefault(const Hctx *context);
/* Context is the reserved frame on the current guarded kernel stack.
   Entry discards the current C continuation; repeated returns reuse this frame. */
NORETURN void hal_user_enter(Hctx *context);
/* Called with all user integer and FP state saved, on the current kernel stack.
   IRQs remain disabled until the core chooses a safe scheduling boundary. */
NORETURN void usertrap(Hctx *context);
#endif
#endif
