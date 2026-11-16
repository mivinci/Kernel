#ifndef _ABI_SIGNAL_H
#define _ABI_SIGNAL_H
#define SIGNAL_DEPTH     2
#define SIGNAL_COUNT     32
#define SIGNAL_DEFAULT   0
#define SIGNAL_IGNORE    1
#define SIGNAL_HANDLER   2
#define SIGNAL_RESTART   1
#define SIGNAL_NODEFER   2
#define SIGNAL_RESETHAND 4
#define SIGNAL_SIGINFO   8
#define SIGNAL_NOCLDSTOP 16
#define SIGNAL_NOCLDWAIT 32
#ifndef __ASSEMBLER__
#include <types.h>
typedef struct Sigact Sigact;
struct Sigact
{
  u64 kind, handler, trampoline, mask, flags, reserved;
};
STATIC_ASSERT(sizeof(Sigact) == 48, sigact_size);
STATIC_ASSERT(offsetof(Sigact, kind) == 0, sigact_kind);
STATIC_ASSERT(offsetof(Sigact, handler) == 8, sigact_handler);
STATIC_ASSERT(offsetof(Sigact, trampoline) == 16, sigact_trampoline);
STATIC_ASSERT(offsetof(Sigact, mask) == 24, sigact_mask);
STATIC_ASSERT(offsetof(Sigact, flags) == 32, sigact_flags);
STATIC_ASSERT(offsetof(Sigact, reserved) == 40, sigact_reserved);
#endif
#endif
