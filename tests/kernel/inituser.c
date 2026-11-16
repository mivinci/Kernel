#include "adopt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
extern const u8 adopt_parent[], adopt_parent_end[];
volatile u64    init_trace[32];
static Elfwork  preparation;
static NORETURN void
failed(u64 code)
{
  init_trace[1] = code;
  for (;;)
    __sys_yield();
}
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      failed(n);                                                                                   \
  } while (0)
int
main(int argc, char **argv)
{
  Handle   pid, parent, channel, pending = 0, leaf = 0, leaf_source = 0, collected;
  u64      image, kind, code, sequence;
  u32      i;
  Msg      words;
  Ipcmsg   message;
  Queryrec records[4];
  u64      written, next;
  REQUIRE(argc == 1, 171);
  REQUIRE(!__sys_getid(&pid, &image, &parent) && pid == 1 && image == 1 && !parent &&
              getpid() == 1 && getppid() == 0,
          172);
  init_trace[8] = (u64) pid;
  init_trace[9] = image;
  init_trace[10] = (u64) parent;
  if (!strcmp(argv[0], "fatal"))
    exit(42);
  REQUIRE(!strcmp(argv[0], "init"), 173);
  init_trace[0] = 1;
  REQUIRE(!__sys_connect(bootresource(sysboot(), ADOPT_SELF_CONTROL), CONNECT_CALL, 1 | ADOPT_GRANT,
                         0, &channel),
          174);
  REQUIRE(!adoptspawn(adopt_parent, (u64) (adopt_parent_end - adopt_parent), "parent", 128, 8,
                      channel, 1 | ADOPT_GRANT, &preparation, &pid, NULL),
          175);
  init_trace[2] = (u64) pid;
  init_trace[0] = 2;
  while (!pending || !leaf) {
    REQUIRE(!ipc_recv(0, 0, &message) && message.kind == IPC_CALL && message.image == 1 &&
                message.request > 0,
            176);
    if (message.words[0] == ADOPT_PARENT_READY) {
      REQUIRE(message.source == pid && !pending && message.words[1] > 1 && message.words[2] > 1,
              177);
      pending = message.request;
      init_trace[3] = message.words[1];
      init_trace[4] = message.words[2];
    } else {
      REQUIRE(message.words[0] == ADOPT_LEAF_READY && !leaf && message.words[1] == (u64) pid, 178);
      leaf = message.request;
      leaf_source = message.source;
    }
    REQUIRE(!ipc_defer(message.request), 179);
  }
  REQUIRE((u64) leaf_source == init_trace[3] && init_trace[3] != init_trace[4], 180);
  memset(&words, 0, sizeof(words));
  words.words[0] = 1;
  REQUIRE(!ipc_reply(leaf, &words), 181);
  init_trace[0] = 3;
  REQUIRE(!__sys_yield(), 182);
  REQUIRE(__sys_wait((Handle) init_trace[3], WAIT_WNOHANG, &collected, &kind, &code, &sequence) ==
                  -ERR_CHILD &&
              !collected,
          183);
  init_trace[13] = 1;
  REQUIRE(!ipc_reply(pending, &words), 184);
  init_trace[0] = 4;
  for (i = 0; i < 3; i++) {
    REQUIRE(!__sys_wait(-1, 0, &collected, &kind, &code, &sequence) && kind == WAIT_EXITED &&
                sequence == 2,
            185);
    if (collected == (Handle) init_trace[2]) {
      REQUIRE(code == 33 && !(init_trace[6] & 1), 186);
      init_trace[6] |= 1;
    } else if (collected == (Handle) init_trace[3]) {
      REQUIRE(code == 11 && !(init_trace[6] & 2), 187);
      init_trace[6] |= 2;
    } else {
      REQUIRE(collected == (Handle) init_trace[4] && code == 22 && !(init_trace[6] & 4), 188);
      init_trace[6] |= 4;
    }
    init_trace[5]++;
  }
  for (i = 0; i < 2; i++) {
    Handle waiter, child, management, waiting = 0, waking = 0;
    u32    n, found = 0;
    int    status;
    isize  prepared;
    init_trace[0] = 40 + i * 10;
    prepared =
        adoptspawn(adopt_parent, (u64) (adopt_parent_end - adopt_parent), i ? "ready" : "blocked",
                   128, 8, channel, 1 | ADOPT_GRANT, &preparation, &waiter, &management);
    init_trace[11] = (u64) -prepared;
    REQUIRE(!prepared && management, 194);
    init_trace[20 + i * 2] = (u64) waiter;
    while (!waiting || (i && !waking)) {
      REQUIRE(!ipc_recv(0, 0, &message) && message.kind == IPC_CALL && message.image == 1, 195);
      if (message.words[0] == ADOPT_WAIT_READY) {
        REQUIRE(message.source == waiter && !waiting, 196);
        waiting = message.request;
        init_trace[21 + i * 2] = message.words[1];
      } else {
        REQUIRE(i && message.words[0] == ADOPT_LEAF_READY && message.words[1] == (u64) waiter &&
                    !waking,
                197);
        waking = message.request;
      }
      REQUIRE(!ipc_defer(message.request), 198);
    }
    child = (Handle) init_trace[21 + i * 2];
    REQUIRE(child > 1 && !ipc_reply(waiting, &words), 199);
    REQUIRE(!__sys_yield(), 200);
    REQUIRE(!__sys_query(management, QUERY_PROC, 0, records, 1, &written, &next) && written == 1 &&
                !next && records[0].data[3] == PROCESS_BLOCKED &&
                records[0].data[12] == PROCESS_WAIT_CHILD &&
                records[0].data[13] == WAIT_SELECT_GROUP && records[0].data[14] == 1 &&
                !records[0].data[4],
            201);
    init_trace[24 + i] = 1;
    /* Give the trusted fixture an exact point to inspect the held lease. */
    REQUIRE(!__sys_yield(), 202);
    if (i) {
      REQUIRE(!ipc_reply(waking, &words) && !__sys_yield(), 203);
      REQUIRE(!__sys_query(management, QUERY_PROC, 0, records, 1, &written, &next) &&
                  written == 1 && records[0].data[3] == PROCESS_READY &&
                  records[0].data[12] == PROCESS_WAIT_CHILD,
              204);
      init_trace[27] = 1;
    }
    REQUIRE(!__sys_terminate(management), 205);
    init_trace[26] |= (u64) 1 << i;
    for (n = 0; n < 2; n++) {
      pid_t result = wait(&status);
      if (result == waiter) {
        REQUIRE(WIFSIGNALED(status) && WTERMSIG(status) == 9 && !(found & 1), 206);
        init_trace[29] |= (u64) 1 << i;
        found |= 1;
      } else {
        REQUIRE(result == child && WIFEXITED(status) && WEXITSTATUS(status) == (i ? 11U : 22U) &&
                    !(found & 2),
                207);
        found |= 2;
      }
      init_trace[5]++;
      init_trace[28]++;
    }
    REQUIRE(found == 3 && !__sys_free(management), 208);
    REQUIRE(waitpid(waiter, NULL, WNOHANG) == -1 && errno == ECHILD, 209);
    /* The fixture scans 8/192 Prep slots on each CPU handoff. Complete one
       bounded sweep before reusing this deliberately eight-slot init table. */
    for (n = 0; n < 24; n++)
      REQUIRE(!__sys_yield(), 210);
    {
      u64 cursor = 0, total = 0, row;
      do {
        REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, records, 4, &written, &next), 211);
        for (row = 0; row < written; row++)
          REQUIRE(records[row].data[1] != 14, 212);
        total += written;
        cursor = next;
      } while (cursor);
      REQUIRE(total == 5, 213);
      init_trace[12] = total;
    }
  }
  init_trace[30] = 1;
  REQUIRE(__sys_wait(-1, WAIT_WNOHANG, &collected, &kind, &code, &sequence) == -ERR_CHILD &&
              !collected,
          189);
  REQUIRE(!ipc_recv(EVENT_CHILD, RECV_NONBLOCK | RECV_EVENTS_ONLY, &message) &&
              message.kind == IPC_EVENT && message.words[0] == EVENT_CHILD,
          190);
  init_trace[7] = message.words[0];
  REQUIRE(!__sys_free(channel), 191);
  init_trace[14] = 1;
  REQUIRE(!__sys_query(0, QUERY_RQ, 0, records, 4, &written, &next) && !written && !next, 192);
  init_trace[15] = 1;
  init_trace[0] = 99;
  for (;;)
    REQUIRE(!__sys_yield(), 193);
}
