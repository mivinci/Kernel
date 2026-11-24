/* Real syscall/continuation payload; job transitions come from the CPU fixture. */
#include <sys/boot.h>
#include <sys/channel.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <abi/proc.h>
#include <abi/time.h>
#include "private/start.h"
#include "private/syscall.h"
volatile u64  jobtrace[8];
static Handle nested;
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
  Msg    words = {{UINT64_C(0x4e455354), 0, 0, 0}};
  Ipcmsg result;
  jobtrace[3] = signum;
  jobtrace[5]++;
  if (nested) {
    if (ipc_call(nested, &words, CALL_ASYNC, &result) || !result.request ||
        ipc_rqcancel(result.request) || ipc_rqack(result.request, ACK_RESULT | ACK_RELEASE))
      __sys_exit(203);
    jobtrace[1] = 1;
  }
}
static void
restartwait(int signum)
{
  if (signum != SIGUSR1)
    __sys_exit(204);
  if (jobtrace[1] == 11) {
    int status;
    if (waitpid(-1, &status, 0) <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 42)
      __sys_exit(205);
  } else if ((!jobtrace[5] && setpgid(0, 0)) || getpgrp() != getpid())
    __sys_exit(204);
  jobtrace[5]++;
}
static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 address, u64 flags)
{
  if ((signum != SIGNAL_USR1 && signum != SIGNAL_CONT && signum != SIGNAL_TSTP &&
       signum != SIGNAL_CHLD) ||
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
  Handle   peer = bootresource(sysboot(), 0x100), child = 0, result, selector;
  u64      kind, code, sequence, generation, written, next, i, state, variant;
  u32      mode;
  isize    error;
  Sigact   action;
  int      status;
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
    if (mode == 1 && child >= 2) {
      CHECK(!install(SIGNAL_USR1, SIGNAL_HANDLER));
      jobtrace[0] = 1;
      error = ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY, &message);
      if (child == 2) {
        CHECK(error == -ERR_INTERRUPTED && jobtrace[3] == SIGNAL_USR1 && jobtrace[5] == 1);
        CHECK(!ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY | RECV_NONBLOCK, &message));
      } else
        CHECK(!error);
      CHECK(message.kind == IPC_EVENT && message.words[0] == ((u64) 1 << 32));
      jobtrace[6] = child;
      if (child == 3) {
        while (!jobtrace[1])
          jobtrace[2]++;
        CHECK(jobtrace[3] == SIGNAL_USR1 && jobtrace[5] == 1);
      }
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
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
    if (child) {
      CHECK(!install(SIGNAL_USR1, SIGNAL_HANDLER));
      if (child == 7)
        nested = peer;
      jobtrace[0] = 1;
      if (child == 5 || child == 6) {
        error = ipc_send(peer, &words);
        CHECK(error == (child == 5 ? -ERR_INTERRUPTED : 0) && jobtrace[5] == 1);
      } else {
        result = 0;
        error = ipc_call(peer, &words, child == 3 || child == 4 || child >= 8 ? CALL_ASYNC : 0,
                         &message);
        if (child == 3 || child == 4 || child >= 8) {
          CHECK(!error && message.request > 0);
          result = message.request;
          error = ipc_rqwait(result, child >= 8 ? RQ_FINISH : 0, &message);
        }
        if (child == 1 || child == 3 || child == 7 || child == 8) {
          CHECK(error == -ERR_INTERRUPTED && jobtrace[3] == SIGNAL_USR1 && jobtrace[5] == 1);
          if (child != 3 && child != 8)
            result = message.request;
          CHECK(result > 0);
          jobtrace[4] = (u64) result;
          jobtrace[6] = 1;
          CHECK(!ipc_rqwait(result, child >= 8 ? RQ_FINISH : 0, &message));
        } else
          CHECK(!error && jobtrace[5] == 1);
        if (child >= 8) {
          CHECK(!message.kind && !message.request && !message.words[0]);
          CHECK(!ipc_rqpoll(result, 0, &message));
        }
        CHECK(message.words[0] == words.words[0] && !message.request);
        if (result)
          CHECK(!ipc_rqack(result, ACK_RESULT | ACK_RELEASE));
      }
      jobtrace[6] = 2;
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
    jobtrace[0] = 1;
    CHECK(!ipc_call(peer, &words, 0, &message) && message.words[0] == words.words[0] &&
          !message.request);
    jobtrace[3] = message.words[0];
  } else if (mode == 4 || mode == 9) {
    if (mode == 4 && child == 2) {
      CHECK(!install(SIGNAL_USR1, SIGNAL_HANDLER));
      CHECK(!ipc_recv(0, 0, &message) && message.kind == IPC_CALL);
      jobtrace[0] = 1;
      while (!jobtrace[1])
        jobtrace[2]++;
      CHECK(ipc_reply_recv(message.request, &words, (u64) 1 << 32, RECV_EVENTS_ONLY, &answer) ==
                -ERR_INTERRUPTED &&
            jobtrace[5] == 1);
      CHECK(ipc_reply(message.request, &words) == -ERR_BAD_HANDLE);
      CHECK(!ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY | RECV_NONBLOCK, &answer) &&
            answer.kind == IPC_EVENT && answer.words[0] == ((u64) 1 << 32));
      jobtrace[6] = 1;
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
    if (mode == 4 && child == 1) {
      CHECK(!ipc_recv(0, 0, &message) && message.kind == IPC_SEND && !message.request &&
            message.words[0] == words.words[0]);
      jobtrace[0] = 1;
      while (!jobtrace[1])
        jobtrace[2]++;
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
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
    struct tms usage;
    action.kind = SIGNAL_HANDLER;
    action.handler = (u64) handler;
    action.trampoline = (u64) trampoline;
    action.mask = action.reserved = 0;
    action.flags = jobtrace[1] ? SIGNAL_NOCLDSTOP : 0;
    CHECK(!__sys_sigaction(SIGNAL_CHLD, &action, NULL) &&
          !__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_CHLD - 1), &state));
    jobtrace[0] = 1;
    CHECK(!__sys_wait(child, WAIT_WUNTRACED, &result, &kind, &code, &sequence) && result == child &&
          kind == WAIT_STOPPED && code == 19 && sequence == 2);
    jobtrace[3] = 1;
    jobtrace[4] = sequence;
    CHECK(!__sys_wait(child, WAIT_WCONTINUED, &result, &kind, &code, &sequence) &&
          result == child && kind == WAIT_CONTINUED && !code && sequence == 3);
    jobtrace[3] = 2;
    jobtrace[5] = sequence;
    CHECK(!__sys_query(0, QUERY_TIMES, 0, &row, 1, &written, &next) && written == 1 && !next &&
          !row.data[2] && !row.data[3]);
    CHECK(times(&usage) >= 0 && !usage.tms_cutime && !usage.tms_cstime);
    CHECK(!__sys_wait(child, 0, &result, &kind, &code, &sequence) && result == child &&
          kind == WAIT_EXITED && code == 42 && sequence == 4);
    jobtrace[6] = sequence;
    {
      Queryrec collected;
      CHECK(!__sys_query(0, QUERY_TIMES, 0, &collected, 1, &written, &next) && written == 1 &&
            !next && (collected.data[2] || collected.data[3]));
      CHECK(times(&usage) >= 0 && usage.tms_cutime == (clock_t) (collected.data[2] / 10000000) &&
            usage.tms_cstime == (clock_t) (collected.data[3] / 10000000));
      CHECK(__sys_wait(child, WAIT_WNOHANG, &result, &kind, &code, &sequence) == -ERR_CHILD);
      CHECK(!__sys_query(0, QUERY_TIMES, 0, &row, 1, &written, &next) && written == 1 && !next &&
            row.data[2] == collected.data[2] && row.data[3] == collected.data[3]);
    }
    jobtrace[3] = 3;
  } else if (mode == 8) {
    if (child && jobtrace[1]) {
      variant = jobtrace[1];
      selector = variant == 3 || variant == 4 || variant >= 7 ? 0 : child;
      if (variant >= 10) {
        struct sigaction restart = {0};
        restart.sa_handler = restartwait;
        restart.sa_flags = SA_RESTART;
        CHECK(!sigaction(SIGUSR1, &restart, NULL) && getpgrp() != getpid());
      } else
        CHECK(!install(SIGNAL_USR1, variant == 7 ? SIGNAL_IGNORE : SIGNAL_HANDLER));
      if (variant == 5)
        CHECK(!install(SIGNAL_CHLD, SIGNAL_HANDLER));
      if (variant == 8)
        CHECK(!__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_USR1 - 1), &state));
      jobtrace[0] = 1;
      if (variant == 4 || variant == 6 || variant >= 10) {
        status = 0x1234;
        errno = 0;
        result = waitpid(selector, &status, 0);
        if (variant == 4) {
          CHECK(result == -1 && errno == EINTR && status == 0x1234 && jobtrace[5] == 1);
          jobtrace[6] = 1;
          result = waitpid(selector, &status, 0);
        }
        if (variant == 11)
          CHECK(result == -1 && errno == ECHILD && status == 0x1234);
        else
          CHECK(result == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
      } else {
        error = __sys_wait(selector, 0, &result, &kind, &code, &sequence);
        if (variant == 1 || variant == 3 || variant == 5 || variant == 9) {
          CHECK(error == -ERR_INTERRUPTED && !result && !kind && !code && !sequence &&
                jobtrace[5] == 1);
          if (variant == 5) {
            CHECK(jobtrace[3] == SIGNAL_CHLD &&
                  !__sys_wait(child, WAIT_WUNTRACED | WAIT_WNOHANG, &result, &kind, &code,
                              &sequence) &&
                  result == child && kind == WAIT_STOPPED && code == SIGNAL_STOP && sequence == 2);
            jobtrace[4] = sequence;
            CHECK(!__sys_sigmask(SIGNAL_BLOCK, (u64) 1 << (SIGNAL_CHLD - 1), &state));
          }
          jobtrace[6] = 1;
          error = __sys_wait(selector, 0, &result, &kind, &code, &sequence);
        }
        CHECK(!error && result == child && kind == WAIT_EXITED && code == 42 &&
              sequence == (variant == 5 ? 4U : 2U));
        if (variant == 8) {
          CHECK(!__sys_sigpending(&state) && state == ((u64) 1 << (SIGNAL_USR1 - 1)) &&
                !jobtrace[5]);
          CHECK(!__sys_sigmask(SIGNAL_UNBLOCK, state, &state));
        }
      }
      CHECK(jobtrace[5] == (variant == 7 ? 0U : variant == 10 ? 2U : 1U));
      jobtrace[6] = 2;
      jobtrace[0] = 2;
      jobtrace[7] = 42;
      return 42;
    }
    jobtrace[0] = 1;
    CHECK(!__sys_wait(0, 0, &result, &kind, &code, &sequence));
    return 201; /* This fixture must be killed while its group wait is stopped. */
  }
  jobtrace[0] = 2;
  jobtrace[7] = 42;
  return 42;
}
