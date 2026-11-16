#ifndef _ABI_SIGNAL_H
#define _ABI_SIGNAL_H
#define SIGNAL_FRAME_MAGIC     0x4b53494730303031
#define SIGNAL_FRAME_TF        128
#define SIGNAL_FRAME_FP        416
#define SIGNAL_FRAME_RV_BYTES  688
#define SIGNAL_FRAME_ARM_BYTES 944
#define SIGNAL_DEPTH           2
#define SIGNAL_COUNT           32
#define SIGNAL_DEFAULT         0
#define SIGNAL_IGNORE          1
#define SIGNAL_HANDLER         2
#define SIGNAL_RESTART         1
#define SIGNAL_NODEFER         2
#define SIGNAL_RESETHAND       4
#define SIGNAL_SIGINFO         8
#define SIGNAL_NOCLDSTOP       16
#define SIGNAL_NOCLDWAIT       32
#define SIGNAL_HUP             1
#define SIGNAL_INT             2
#define SIGNAL_QUIT            3
#define SIGNAL_ILL             4
#define SIGNAL_ABRT            6
#define SIGNAL_BUS             7
#define SIGNAL_FPE             8
#define SIGNAL_KILL            9
#define SIGNAL_USR1            10
#define SIGNAL_SEGV            11
#define SIGNAL_USR2            12
#define SIGNAL_PIPE            13
#define SIGNAL_ALRM            14
#define SIGNAL_TERM            15
#define SIGNAL_CHLD            17
#define SIGNAL_CONT            18
#define SIGNAL_STOP            19
#define SIGNAL_TSTP            20
#define SIGNAL_TTIN            21
#define SIGNAL_TTOU            22
#define SIGNAL_WINCH           28
#define SIGNAL_SUPPORTED       0x83f7fefUL
#define SIGNAL_UNMASKABLE      ((1UL << (SIGNAL_KILL - 1)) | (1UL << (SIGNAL_STOP - 1)))
#define SIGNAL_BLOCK           0
#define SIGNAL_UNBLOCK         1
#define SIGNAL_SETMASK         2
#ifndef __ASSEMBLER__
#include <types.h>
typedef struct Sigact  Sigact;
typedef struct Sighead Sighead;
struct Sigact
{
  u64 kind, handler, trampoline, mask, flags, reserved;
};
struct Sighead
{
  u64 magic, bytes, depth, previous, restore_mask, flags, cookie, reserved;
};
STATIC_ASSERT(sizeof(Sighead) == 64, sighead_size);
STATIC_ASSERT(offsetof(Sighead, magic) == 0, sighead_magic);
STATIC_ASSERT(offsetof(Sighead, bytes) == 8, sighead_bytes);
STATIC_ASSERT(offsetof(Sighead, depth) == 16, sighead_depth);
STATIC_ASSERT(offsetof(Sighead, previous) == 24, sighead_previous);
STATIC_ASSERT(offsetof(Sighead, restore_mask) == 32, sighead_mask);
STATIC_ASSERT(offsetof(Sighead, flags) == 40, sighead_flags);
STATIC_ASSERT(offsetof(Sighead, cookie) == 48, sighead_cookie);
STATIC_ASSERT(offsetof(Sighead, reserved) == 56, sighead_reserved);
STATIC_ASSERT(SIGNAL_FRAME_RV_BYTES % 16 == 0 && SIGNAL_FRAME_ARM_BYTES % 16 == 0, sigframe_align);
STATIC_ASSERT(sizeof(Sigact) == 48, sigact_size);
STATIC_ASSERT(offsetof(Sigact, kind) == 0, sigact_kind);
STATIC_ASSERT(offsetof(Sigact, handler) == 8, sigact_handler);
STATIC_ASSERT(offsetof(Sigact, trampoline) == 16, sigact_trampoline);
STATIC_ASSERT(offsetof(Sigact, mask) == 24, sigact_mask);
STATIC_ASSERT(offsetof(Sigact, flags) == 32, sigact_flags);
STATIC_ASSERT(offsetof(Sigact, reserved) == 40, sigact_reserved);
#endif
#endif
