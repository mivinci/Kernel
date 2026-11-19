#include <sys/boot.h>
#include <sys/channel.h>
#include <abi/syscall.h>
#include "private/syscall.h"
volatile u64 sendtrace[16];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return __LINE__;                                                                             \
  } while (0)
static void
handler(u64 signum, const Siginfo *info)
{
  if (info->signum != signum || info->code != SIGNAL_SOURCE_USER || !info->pid || info->reserved0 ||
      info->address || info->reason || info->status || info->reserved[0] || info->reserved[1])
    __sys_exit(200);
  sendtrace[8]++;
  sendtrace[9] = info->pid;
  sendtrace[10] = info->uid;
  sendtrace[11] = info->code;
  sendtrace[12] = signum;
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  if ((signum != SIGNAL_USR1 && signum != SIGNAL_CONT) || function != (u64) handler || flags ||
      context != (void *) (address + SIGNAL_FRAME_TF))
    __sys_exit(201);
  handler(signum, info);
  __sys_sigreturn((void *) address);
}
int
main(int argc, char **argv)
{
  Sigact action;
  Sysret result;
  Ipcmsg message;
  u64    old, sequence, args[7], i;
  u32    mode, masked;
  isize  error;
  CHECK(argc == 2 && argv[0][0] >= '0' && argv[0][0] <= '2' && !argv[0][1] && argv[1][0] >= '0' &&
        argv[1][0] <= '1' && !argv[1][1]);
  mode = (u32) (argv[0][0] - '0');
  masked = (u32) (argv[1][0] - '0');
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) handler;
  action.trampoline = (u64) trampoline;
  action.mask = action.flags = action.reserved = 0;
  CHECK(!__sys_sigaction(SIGNAL_USR1, &action, NULL) &&
        !__sys_sigaction(SIGNAL_CONT, &action, NULL));
  if (masked)
    CHECK(!__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_USR1 - 1), &old));
  sendtrace[0] = 1;
  if (mode == 2) {
    CHECK(!ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY, &message));
    return 202; /* The retained PID 1 must remain blocked and excluded by ALL. */
  }
  if (!mode) {
    while (!sendtrace[1]) {
    }
    if (masked)
      CHECK(!__sys_sigmask(SIGNAL_UNBLOCK, (u64) 1 << (SIGNAL_USR1 - 1), &old));
  } else {
    while (!sendtrace[14]) {
      sequence = sendtrace[1];
      if (sequence == sendtrace[7])
        continue;
      if (!sendtrace[15])
        error = __sys_sigsend((const Sigsel *) &sendtrace[2], sendtrace[4]);
      else {
        for (i = 0; i < 7; i++)
          args[i] = 0;
        args[0] = sendtrace[15] == 2 ? sendtrace[3] : (u64) &sendtrace[2];
        args[1] = sendtrace[4];
        args[2] = sendtrace[15] == 1 ? 1 : 0;
        error = __sys_enter(SYS_SIGSEND, args, &result);
        sendtrace[5] = 0;
        for (i = 0; i < 7; i++)
          sendtrace[5] |= result.values[i];
      }
      sendtrace[6] = (u64) error;
      sendtrace[7] = sequence;
    }
  }
  sendtrace[0] = 2;
  return 42;
}
