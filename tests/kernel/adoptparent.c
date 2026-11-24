#include "adopt.h"
#include <stdlib.h>
#include <string.h>
extern const u8 adopt_leaf[], adopt_leaf_end[];
static Elfwork  preparation;
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      exit(n);                                                                                     \
  } while (0)
int
main(int argc, char **argv, char **envp)
{
  (void) envp;
  Handle ended, live, pid, parent, channel;
  u64    image;
  Msg    words;
  Ipcmsg reply;
  REQUIRE(argc == 1, 161);
  REQUIRE(!__sys_getid(&pid, &image, &parent) && parent == 1 && pid > 1 && image == 1, 162);
  channel = bootresource(sysboot(), ADOPT_ENDPOINT);
  if (!strcmp(argv[0], "blocked") || !strcmp(argv[0], "ready")) {
    Handle collected;
    u64    kind, code, sequence;
    REQUIRE(!adoptspawn(adopt_leaf, (u64) (adopt_leaf_end - adopt_leaf),
                        !strcmp(argv[0], "ready") ? "wake" : "live", 96, 4, channel, 1,
                        &preparation, &live, NULL),
            166);
    words.words[0] = ADOPT_WAIT_READY;
    words.words[1] = (u64) live;
    words.words[2] = 0;
    words.words[3] = 0;
    REQUIRE(!ipc_call(channel, &words, 0, &reply) && reply.words[0] == 1, 167);
    /* Termination must abandon this group lease and saved C continuation,
       including the READY window after the child's result wakes us. */
    __sys_wait(0, 0, &collected, &kind, &code, &sequence);
    exit(168);
  }
  REQUIRE(!strcmp(argv[0], "parent"), 169);
  REQUIRE(!adoptspawn(adopt_leaf, (u64) (adopt_leaf_end - adopt_leaf), "ended", 96, 4, channel, 1,
                      &preparation, &ended, NULL),
          163);
  REQUIRE(!adoptspawn(adopt_leaf, (u64) (adopt_leaf_end - adopt_leaf), "live", 96, 4, channel, 1,
                      &preparation, &live, NULL),
          164);
  words.words[0] = ADOPT_PARENT_READY;
  words.words[1] = (u64) ended;
  words.words[2] = (u64) live;
  words.words[3] = 0;
  REQUIRE(!ipc_call(channel, &words, 0, &reply) && reply.words[0] == 1, 165);
  return 33;
}
