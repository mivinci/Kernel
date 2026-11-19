/* Real syscall/continuation payload; job transitions come from the CPU fixture. */
#include <sys/boot.h>
#include <sys/channel.h>
#include <abi/proc.h>
#include <abi/time.h>
#include "private/start.h"
#include "private/syscall.h"
volatile u64 jobtrace[8];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return __LINE__;                                                                             \
  } while (0)
static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  if (__sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos) || seconds < 0)
    __sys_exit(200);
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}
static void
handler(u64 signum)
{
  jobtrace[3] = signum;
  jobtrace[5]++;
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  if ((signum != SIGNAL_USR1 && signum != SIGNAL_CONT && signum != SIGNAL_TSTP) ||
      info->signum != signum || function != (u64) handler ||
      context != (void *) (address + SIGNAL_FRAME_TF) || flags)
    __sys_exit(202);
  handler(signum);
  __sys_sigreturn((void *) address);
}
static isize
install(u32 signum, u64 kind)
{
  Sigact action;
  action.kind = kind;
  action.handler = kind == SIGNAL_HANDLER ? (u64) handler : 0;
  action.trampoline = kind == SIGNAL_HANDLER ? (u64) trampoline : 0;
  action.mask = action.flags = action.reserved = 0;
  return __sys_sigaction(signum, &action, NULL);
}
int
main(int argc, char **argv)
{
  Ipcmsg   message, answer;
  Msg      words = {{UINT64_C(0x4a4f4253), 0, 0, 0}};
  Queryrec row;
  Handle   peer = bootresource(sysboot(), 0x100), child = 0, result;
  u64      kind, code, sequence, generation, written, next, i, state;
  u32      mode;
  isize    error;
  Sigact   action;
  CHECK(argc == 2 && argv[0] && argv[0][0] >= '0' && argv[0][0] <= '9' && !argv[0][1]);
  mode = (u32) (argv[0][0] - '0');
  for (i = 0; argv[1][i]; i++) {
    CHECK(argv[1][i] >= '0' && argv[1][i] <= '9');
    child = child * 10 + argv[1][i] - '0';
  }
  if (mode == 0) {
    if (child == 1 || child == 2 || child == 7)
      CHECK(!__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_TSTP - 1), &state));
    if (child == 2)
      CHECK(!install(SIGNAL_USR1, SIGNAL_HANDLER));
    if (child == 3 || child == 7) {
      CHECK(!install(SIGNAL_CONT, SIGNAL_HANDLER));
      CHECK(!__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_CONT - 1), &state));
    }
    if (child == 4)
      CHECK(!install(SIGNAL_CONT, SIGNAL_IGNORE));
    if (child == 5 || child == 6)
      CHECK(!install(SIGNAL_TSTP, child == 5 ? SIGNAL_HANDLER : SIGNAL_IGNORE));
    jobtrace[0] = 1;
    while (!jobtrace[1])
      jobtrace[2]++;
    if (child == 1) {
      CHECK(!__sys_sigmask(SIGNAL_UNBLOCK, (u64) 1 << (SIGNAL_TSTP - 1), &state));
      jobtrace[3] = 1;
    } else if (child == 2) {
      CHECK(__sys_sigsuspend(0) == -ERR_INTERRUPTED && jobtrace[3] == SIGNAL_USR1);
      CHECK(!__sys_sigmask(SIGNAL_BLOCK, 0, &state) && (state & ((u64) 1 << (SIGNAL_TSTP - 1))));
      jobtrace[4] = state;
    } else if (child == 3 || child == 7) {
      CHECK(!__sys_sigpending(&state));
      jobtrace[4] = state;
      CHECK(!__sys_sigmask(
          SIGNAL_UNBLOCK, ((u64) 1 << (SIGNAL_TSTP - 1)) | ((u64) 1 << (SIGNAL_CONT - 1)), &state));
      CHECK(jobtrace[3] == SIGNAL_CONT && jobtrace[5] == 1);
    }
  } else if (mode == 1 || mode == 2) {
    if (mode == 1 && child == 1) {
      action.kind = SIGNAL_HANDLER;
      action.handler = (u64) handler;
      action.trampoline = (u64) trampoline;
      action.mask = action.flags = action.reserved = 0;
      CHECK(!__sys_sigaction(SIGNAL_USR1, &action, NULL));
      jobtrace[0] = 1;
      CHECK(__sys_sigsuspend(0) == -ERR_INTERRUPTED && jobtrace[3] == SIGNAL_USR1);
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
    if (mode == 2) {
      jobtrace[4] = now() + UINT64_C(50000000);
      CHECK(!__sys_alarmset(jobtrace[4], &generation));
    }
    jobtrace[0] = 1;
    CHECK(!ipc_recv(mode == 1 ? (u64) 1 << 32 : EVENT_TIMER, RECV_EVENTS_ONLY, &message) &&
          message.kind == IPC_EVENT && !message.request && !message.source);
    jobtrace[3] = message.words[0];
    CHECK(message.words[0] == (mode == 1 ? (u64) 1 << 32 : EVENT_TIMER));
  } else if (mode == 3) {
    jobtrace[0] = 1;
    CHECK(!ipc_call(peer, &words, 0, &message) && message.words[0] == words.words[0] &&
          !message.request);
    jobtrace[3] = message.words[0];
  } else if (mode == 4 || mode == 9) {
    CHECK(!ipc_recv(0, 0, &message) && message.kind == IPC_CALL);
    jobtrace[0] = 1;
    while (!jobtrace[1])
      jobtrace[2]++;
    if (mode == 9) {
      CHECK(ipc_call(peer, &words, 0, &answer) == -ERR_DEADLOCK && !answer.request);
      jobtrace[5] = 1;
    }
    CHECK(!ipc_rqcheck(message.request, &state));
    jobtrace[3] = state;
    if (state == RQ_CHECK_DRAINING)
      CHECK(!ipc_rqfinish(message.request));
    else
      CHECK(state == RQ_CHECK_LIVE && !ipc_reply(message.request, &words));
    if (mode == 9) {
      jobtrace[6] = 1;
      error = ipc_call(peer, &words, 0, &answer);
      CHECK(error == -ERR_PEER && !answer.request);
      jobtrace[5] = 2;
    }
  } else if (mode == 5) {
    CHECK(!ipc_notify(peer, (u64) 1 << 32));
  } else if (mode == 6) {
    CHECK(!__sys_query(peer, QUERY_PROC, 0, &row, 1, &written, &next) && written == 1 && !next &&
          row.pid == child && row.data[3] == PROCESS_STOPPED);
    jobtrace[6] = row.data[12];
    CHECK(!__sys_terminate(peer) && !__sys_query(peer, QUERY_LIFE, 0, &row, 1, &written, &next) &&
          written == 1 && !next && row.data[1] == 2 && row.data[2] == 9);
  } else if (mode == 7) {
    jobtrace[0] = 1;
    CHECK(!__sys_wait(child, WAIT_WUNTRACED, &result, &kind, &code, &sequence) && result == child &&
          kind == WAIT_STOPPED && code == 19 && sequence == 2);
    jobtrace[3] = 1;
    jobtrace[4] = sequence;
    CHECK(!__sys_wait(child, WAIT_WCONTINUED, &result, &kind, &code, &sequence) &&
          result == child && kind == WAIT_CONTINUED && !code && sequence == 3);
    jobtrace[3] = 2;
    jobtrace[5] = sequence;
    CHECK(!__sys_wait(child, 0, &result, &kind, &code, &sequence) && result == child &&
          kind == WAIT_EXITED && code == 42 && sequence == 4);
    jobtrace[3] = 3;
    jobtrace[6] = sequence;
  } else if (mode == 8) {
    jobtrace[0] = 1;
    CHECK(!__sys_wait(0, 0, &result, &kind, &code, &sequence));
    return 201; /* This fixture must be killed while its group wait is stopped. */
  }
  jobtrace[0] = 2;
  jobtrace[7] = 42;
  return 42;
}
