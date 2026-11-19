#include "ttyuser.h"
#include "../../sbin/tty/wait.h"
#include "private/syscall.h"
#include <sys/channel.h>
#include <abi/time.h>
#include <unistd.h>

volatile u64   ttytrace[8];
static Termarg mode;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(232);                                                                                  \
  } while (0)

static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  CHECK(!__sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos) && seconds >= 0 &&
        nanos < UINT64_C(1000000000) &&
        (u64) seconds <= (UINT64_MAX - nanos) / UINT64_C(1000000000));
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}

int
main(int argc, char **argv)
{
  Ttywait wait;
  Ipcmsg  message;
  u64     current, next, generation, deadline;
  CHECK(!argc && argv && !argv[0]);
  ttyusercheck();
  ttytrace[0] = 1;
  mode.cflag = TTY_CS8;
  current = now();
  CHECK(!ttywaitinit(&wait, &mode, 32, 0, 0, current));
  CHECK(ttywaitcheck(&wait, 0, current, &next) == 1 && !next);
  mode.cc[TTY_VMIN] = 3;
  CHECK(!ttywaitinit(&wait, &mode, 2, 0, 0, current));
  CHECK(!ttywaitcheck(&wait, 1, current, &next) && !next);
  CHECK(ttywaitcheck(&wait, 2, current, &next) == 1 && !next);
  mode.cc[TTY_VMIN] = 0;
  mode.cc[TTY_VTIME] = 1;
  CHECK(!ttywaitinit(&wait, &mode, 32, 0, 0, current));
  CHECK(!ttywaitcheck(&wait, 0, current, &next) && next == current + UINT64_C(100000000));
  deadline = next;
  ttytrace[2] = deadline;
  CHECK(!__sys_alarmset(deadline, &generation));
  CHECK(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &message) && message.kind == IPC_EVENT &&
        !message.source && !message.request && !message.image && message.words[0] == EVENT_TIMER);
  current = now();
  CHECK(current >= deadline && ttywaitcheck(&wait, 0, current, &next) == 1 && !next);
  ttytrace[3] = current;
  ttytrace[1] = 1;
  mode.cc[TTY_VMIN] = 3;
  CHECK(!ttywaitinit(&wait, &mode, 2, 0, 0, current));
  CHECK(!ttywaitcheck(&wait, 0, current + 1000, &next) && !next);
  CHECK(!ttywaitbyte(&wait, current + 1000));
  CHECK(!ttywaitcheck(&wait, 1, current + 1001, &next) && next == current + UINT64_C(100001000));
  CHECK(!ttywaitbyte(&wait, current + 2000));
  CHECK(ttywaitcheck(&wait, 2, current + 2000, &next) == 1 && !next);
  ttytrace[4] = 1;
  CHECK(!ttywaitinit(&wait, &mode, 32, 0, 1, current));
  deadline = wait.deadline;
  CHECK(ttywaitbyte(&wait, deadline) == 1 && wait.deadline == deadline);
  CHECK(ttywaitcheck(&wait, 1, deadline, &next) == 1 && !next);
  CHECK(!ttywaitinit(&wait, &mode, 32, 1, 0, current));
  CHECK(ttywaitcheck(&wait, 0, current, &next) == -ERR_WOULD_BLOCK && !next);
  CHECK(ttywaitcheck(&wait, 1, current, &next) == 1 && !next);
  ttytrace[5] = 1;
  current = now();
  CHECK(!ttywaitinit(&wait, &mode, 32, 0, 1, current));
  CHECK(!ttywaitcheck(&wait, 1, current, &next) && next == current + UINT64_C(100000000));
  deadline = next;
  CHECK(!__sys_alarmset(deadline, &generation));
  CHECK(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &message) && message.kind == IPC_EVENT &&
        !message.source && !message.request && !message.image && message.words[0] == EVENT_TIMER);
  current = now();
  CHECK(current >= deadline && ttywaitcheck(&wait, 1, current, &next) == 1 && !next);
  ttytrace[1]++;
  mode.lflag = TTY_ICANON;
  CHECK(!ttywaitinit(&wait, &mode, 32, 0, 0, current));
  CHECK(!ttywaitcheck(&wait, 0, current, &next) && !next);
  CHECK(ttywaitcheck(&wait, 1, current, &next) == 1 && !next);
  ttytrace[6] = 1;
  ttytrace[7] = 42;
  return 42;
}
