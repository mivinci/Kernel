#include "private/syscall.h"
#include <abi/boot.h>

volatile u64     faulttrace[16];
u64              fault_data[2];
extern const u8  fault_read[], fault_write[], fault_bad[], fault_resume[], fault_readonly[];
extern const u64 fault_alignment_pc, fault_alignment_signal, fault_alignment_reason;
void             faulttrigger(u64 mode);
void             faultget(const void *context, u64 out[3]);
void             faultsetpc(void *context, u64 pc);
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      faulttrace[12] = __LINE__;                                                                   \
      __sys_exit(190);                                                                             \
    }                                                                                              \
  } while (0)

static void
handler(u64 signum, const Siginfo *info, void *context)
{
  u64      state[3];
  Sighead *head = (Sighead *) ((u8 *) info - sizeof(Sighead));
  faulttrace[13] = info->address;
  faulttrace[14] = info->reason;
  faulttrace[15] = info->signum;
  CHECK(info->signum == signum && signum == faulttrace[5] && info->code == SIGNAL_SOURCE_FAULT &&
        !info->pid && !info->uid && !info->reserved0 && !info->status && !info->reserved[0] &&
        !info->reserved[1] && info->address == faulttrace[4] && info->reason == faulttrace[6]);
  faultget(context, state);
  CHECK(state[0] == faulttrace[3] && head->depth == faulttrace[2] + 1 && head->cookie);
  faulttrace[2]++;
  faulttrace[7] = state[1];
  faulttrace[8] = state[2];
  faulttrace[9] = state[0];
  faulttrace[10] = head->depth;
  if (faulttrace[1] >= 9) {
    faulttrigger(0);
    __sys_exit(191);
  }
  faultsetpc(context, (u64) fault_resume);
}

static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  CHECK(function == (u64) handler && flags == (faulttrace[1] == 10 ? SIGNAL_NODEFER : 0) &&
        (u64) info == address + 64 && (u64) context == address + SIGNAL_FRAME_TF);
  handler(signum, info, context);
  __sys_sigreturn((void *) address);
}

int
main(int argc, char **argv)
{
  Sigact action;
  u64    mode, old;
  CHECK(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= ':' && !argv[0][1]);
  mode = (u64) (argv[0][0] - '0');
  faulttrace[1] = mode;
  faulttrace[3] = (u64) fault_read;
  faulttrace[4] = 0x400000;
  faulttrace[5] = SIGNAL_SEGV;
  faulttrace[6] = SIGNAL_FAULT_MAPERR;
  if (mode == 1) {
    faulttrace[3] = (u64) fault_write;
    faulttrace[4] = (u64) fault_readonly;
    faulttrace[6] = SIGNAL_FAULT_ACCERR;
  } else if (mode == 2) {
    faulttrace[3] = faulttrace[4] = (u64) fault_data;
    faulttrace[6] = SIGNAL_FAULT_ACCERR;
  } else if (mode == 3 || mode == 4) {
    faulttrace[3] = faulttrace[4] = mode == 3 ? (u64) fault_bad : fault_alignment_pc;
    faulttrace[5] = mode == 3 ? SIGNAL_ILL : fault_alignment_signal;
    faulttrace[6] = mode == 3 ? SIGNAL_FAULT_ILLEGAL : fault_alignment_reason;
  }
  action.kind = mode == 5 ? SIGNAL_DEFAULT : mode == 6 ? SIGNAL_IGNORE : SIGNAL_HANDLER;
  action.handler = action.kind == SIGNAL_HANDLER ? (u64) handler : 0;
  action.trampoline = action.kind == SIGNAL_HANDLER ? (u64) trampoline : 0;
  action.mask = action.reserved = 0;
  action.flags = mode == 10 ? SIGNAL_NODEFER : 0;
  CHECK(!__sys_sigaction(faulttrace[5], &action, 0));
  if (mode == 7)
    CHECK(!__sys_sigmask(SIGNAL_BLOCK, UINT64_C(1) << (SIGNAL_SEGV - 1), &old));
  faulttrace[0] = 1;
  faulttrigger(mode);
  CHECK(mode < 5 && faulttrace[2] == 1);
  faulttrace[0] = 2;
  faulttrace[11] = 1;
  return 42;
}
