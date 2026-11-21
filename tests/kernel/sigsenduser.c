#include <sys/boot.h>
#include <sys/channel.h>
#include <abi/syscall.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include "private/syscall.h"
volatile u64 sendtrace[16];
static u32   nested;
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
  if (nested && signum == SIGNAL_USR1) {
    if (raise(SIGCONT) || sendtrace[12] != SIGNAL_CONT)
      __sys_exit(204);
    sendtrace[13]++;
  }
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
static volatile u32 actioncalls;
static volatile u32 infocalls;
static void
publicinfo(int signal, siginfo_t *info, void *context)
{
  if (signal != SIGALRM || info->si_signo != signal || info->si_code != SI_USER ||
      info->si_pid != getpid() || info->si_uid != geteuid() || info->si_errno || info->si_addr ||
      info->si_status || !context || errno != EDOM)
    __sys_exit(208);
  infocalls++;
  errno = ERANGE;
}
static void
publichandler(int signal)
{
  sigset_t         mask;
  struct sigaction current;
  if (signal != SIGALRM || errno != EDOM || sigprocmask(SIG_BLOCK, NULL, &mask) ||
      sigismember(&mask, SIGTERM) != 1 || sigaction(SIGALRM, NULL, &current) ||
      current.sa_handler != SIG_DFL)
    __sys_exit(206);
  actioncalls++;
  errno = ERANGE;
}

static int
actioncheck(void)
{
  struct sigaction action = {0}, old;
  sigset_t         mask;
  action.sa_handler = publichandler;
  action.sa_flags = SA_RESETHAND;
  if (sigaddset(&action.sa_mask, SIGTERM) || sigaction(SIGALRM, &action, &old) ||
      old.sa_handler != SIG_DFL || sigaction(SIGALRM, NULL, &old) ||
      old.sa_handler != publichandler || old.sa_mask != action.sa_mask ||
      old.sa_flags != SA_RESETHAND)
    return 0;
  errno = EDOM;
  if (raise(SIGALRM) || actioncalls != 1 || errno != EDOM || sigprocmask(SIG_BLOCK, NULL, &mask) ||
      sigismember(&mask, SIGTERM))
    return 0;
  action.sa_handler = SIG_IGN;
  action.sa_flags = 0;
  if (sigaction(SIGALRM, &action, &action) || action.sa_handler != SIG_DFL || raise(SIGALRM) ||
      actioncalls != 1)
    return 0;
  action.sa_sigaction = publicinfo;
  action.sa_flags = SA_SIGINFO;
  return !sigaction(SIGALRM, &action, &old) && old.sa_handler == SIG_IGN &&
         !sigaction(SIGALRM, NULL, &old) && old.sa_sigaction == publicinfo &&
         old.sa_flags == SA_SIGINFO && !raise(SIGALRM) && infocalls == 1 && errno == EDOM;
}

static int
raisecheck(void)
{
  static const int invalid[] = {0, -1, 5, 32, 33, INT_MAX};
  Sigact           action;
  sigset_t         mask, previous, pending;
  u32              i, checks = 0;
  for (i = 0; i < ARRAY_SIZE(invalid); i++) {
    errno = EDOM;
    CHECK(raise(invalid[i]) == -1 && errno == EINVAL && !sendtrace[8]);
    checks++;
  }
  errno = EDOM;
  CHECK(!raise(SIGUSR1) && sendtrace[8] == 1 && sendtrace[9] == (u64) getpid() && errno == EDOM);
  checks++;
  CHECK(!sigemptyset(&mask) && !sigaddset(&mask, SIGUSR1) &&
        !sigprocmask(SIG_BLOCK, &mask, &previous) && !previous && !raise(SIGUSR1) &&
        !raise(SIGUSR1) && sendtrace[8] == 1 && !sigpending(&pending) && pending == mask);
  checks++;
  CHECK(!sigprocmask(SIG_UNBLOCK, &mask, NULL) && sendtrace[8] == 2 && !sigpending(&pending) &&
        !pending && errno == EDOM);
  checks++;
  action.kind = SIGNAL_IGNORE;
  action.handler = action.trampoline = action.mask = action.flags = action.reserved = 0;
  CHECK(!__sys_sigaction(SIGNAL_USR1, &action, NULL) && !raise(SIGUSR1) && sendtrace[8] == 2 &&
        !sigpending(&pending) && !pending);
  checks++;
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) handler;
  action.trampoline = (u64) trampoline;
  nested = 1;
  CHECK(!__sys_sigaction(SIGNAL_USR1, &action, NULL) && !raise(SIGUSR1) && sendtrace[8] == 4 &&
        sendtrace[13] == 1 && errno == EDOM);
  checks++;
  nested = 0;
  CHECK(!raise(SIGUSR1) && sendtrace[8] == 5 && sendtrace[12] == SIGNAL_USR1 && errno == EDOM);
  checks++;
  action.kind = SIGNAL_DEFAULT;
  action.handler = action.trampoline = 0;
  CHECK(!__sys_sigaction(SIGNAL_CONT, &action, NULL) && !raise(SIGCONT) && sendtrace[8] == 5 &&
        !sigpending(&pending) && !pending);
  checks++;
  return (int) checks;
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
  int    checks;
  CHECK(argc == 2 && argv[0][0] >= '0' && argv[0][0] <= '5' && !argv[0][1] && argv[1][0] >= '0' &&
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
  if (mode == 3) {
    checks = raisecheck();
    sendtrace[1] = (u64) checks;
    if (checks != 13)
      return checks;
    if (!actioncheck())
      return 207;
    sendtrace[0] = 2;
    return 42;
  }
  if (mode == 4) {
    raise(SIGKILL);
    return 205;
  }
  if (mode == 5) {
    action.kind = SIGNAL_DEFAULT;
    action.handler = action.trampoline = 0;
    CHECK(!__sys_sigaction(SIGNAL_CONT, &action, NULL) && !raise(SIGSTOP));
    sendtrace[1] = 1;
    sendtrace[0] = 2;
    return 42;
  }
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
