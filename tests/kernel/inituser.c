#include "adopt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern const u8 adopt_parent[], adopt_parent_end[];
volatile u64    init_trace[16];
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
                      channel, 1 | ADOPT_GRANT, &preparation, &pid),
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
