#include <unistd.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 suspend_trace[32];
extern u64   frame_fp(u64 mode);
#define BIT(n) ((u64) 1 << ((n) - 1))
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      suspend_trace[31] = (n);                                                                     \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
static void
handler(u64 signum, const Siginfo *info, void *context)
{
  Sighead *head = (Sighead *) ((u8 *) context - SIGNAL_FRAME_TF);
  u64      mode = suspend_trace[1], old, mask;
  UNUSED(info);
  suspend_trace[4]++;
  REQUIRE(frame_fp(head->depth == 2 ? 3 : 2) == (head->depth == 2 ? 2U : 1U), 181);
  if (head->depth == 1 && signum == SIGNAL_USR1 && (mode == 2 || mode == 6)) {
    suspend_trace[0] = 3;
    REQUIRE(__sys_sigsuspend(BIT(SIGNAL_USR1)) == -ERR_INTERRUPTED, 182);
    suspend_trace[6]++;
    REQUIRE(frame_fp(2) == 2, 183);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
                mask == (BIT(SIGNAL_USR2) | BIT(SIGNAL_USR1) | BIT(SIGNAL_TERM)),
            184);
  }
  if (head->depth == 2 && mode == 6) {
    suspend_trace[0] = 4;
    __sys_sigsuspend(0);
    REQUIRE(0, 185);
  }
  if (mode != 1)
    REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, 1, &old), 186);
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  Sighead *head = (Sighead *) address;
  u64      mask, expected;
  REQUIRE(head->magic == SIGNAL_FRAME_MAGIC && head->cookie && !head->flags && !head->reserved &&
              head->depth >= 1 && head->depth <= 2 && flags == SIGNAL_SIGINFO &&
              (u64) info == address + 64 && (u64) context == address + 128 &&
              function == suspend_trace[2],
          187);
  expected =
      BIT(SIGNAL_USR1) | BIT(SIGNAL_USR2) | BIT(signum == SIGNAL_USR1 ? SIGNAL_TERM : SIGNAL_PIPE);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) && mask == expected, 188);
  REQUIRE(head->restore_mask == (head->depth == 1
                                     ? BIT(SIGNAL_USR1)
                                     : BIT(SIGNAL_USR1) | BIT(SIGNAL_USR2) | BIT(SIGNAL_TERM)),
          189);
  ((void (*)(u64, const Siginfo *, void *))(uintptr_t) function)(signum, info, context);
  if (suspend_trace[1] == 7)
    head->restore_mask = BIT(SIGNAL_PIPE);
  __sys_sigreturn((void *) address);
}
int
main(int argc, char **argv)
{
  u64    mode, old, mask, pending;
  Sigact action;
  Ipcmsg event;
  REQUIRE(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '8', 190);
  mode = (u64) (argv[0][0] - '0');
  suspend_trace[1] = mode;
  suspend_trace[2] = (u64) (uintptr_t) handler;
  suspend_trace[3] = (u64) (uintptr_t) trampoline;
  REQUIRE(frame_fp(1) == 0 && !__sys_sigmask(SIGNAL_SETMASK, BIT(SIGNAL_USR1), &old) && !old, 191);
  action.kind = SIGNAL_IGNORE;
  action.handler = action.trampoline = action.mask = action.flags = action.reserved = 0;
  REQUIRE(!__sys_sigaction(SIGNAL_WINCH, &action, NULL), 192);
  suspend_trace[0] = 1;
  REQUIRE(!__sys_yield(), 193);
  if (mode == 3) {
    REQUIRE(__sys_sigsuspend((u64) 1 << 63) == -ERR_INVALID, 194);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) && mask == BIT(SIGNAL_USR1), 195);
    suspend_trace[7] = 1;
  }
  if (mode == 1)
    REQUIRE(!ipc_notify(bootresource(sysboot(), 257), (u64) 1 << 32), 196);
  suspend_trace[0] = 2;
  REQUIRE(__sys_sigsuspend(BIT(SIGNAL_USR2) | SIGNAL_UNMASKABLE) == -ERR_INTERRUPTED, 197);
  suspend_trace[5]++;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
              mask == BIT(mode == 7 ? SIGNAL_PIPE : SIGNAL_USR1),
          198);
  REQUIRE(frame_fp(1) == 1, 199);
  REQUIRE(suspend_trace[4] == (mode == 1 || mode == 2 ? 2U : 1U), 200);
  REQUIRE(!__sys_sigpending(&pending) && !pending, 201);
  if (mode == 1)
    REQUIRE(!ipc_recv((u64) 1 << 32, RECV_NONBLOCK | RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == ((u64) 1 << 32),
            202);
  suspend_trace[0] = 99;
  return 42;
}
