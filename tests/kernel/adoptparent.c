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
main(int argc, char **argv)
{
  Handle ended, live, pid, parent, channel;
  u64    image;
  Msg    words;
  Ipcmsg reply;
  REQUIRE(argc == 1 && !strcmp(argv[0], "parent"), 161);
  REQUIRE(!__sys_getid(&pid, &image, &parent) && parent == 1 && pid > 1 && image == 1, 162);
  channel = bootresource(sysboot(), ADOPT_ENDPOINT);
  REQUIRE(!adoptspawn(adopt_leaf, (u64) (adopt_leaf_end - adopt_leaf), "ended", 64, 4, channel, 1,
                      &preparation, &ended),
          163);
  REQUIRE(!adoptspawn(adopt_leaf, (u64) (adopt_leaf_end - adopt_leaf), "live", 64, 4, channel, 1,
                      &preparation, &live),
          164);
  words.words[0] = ADOPT_PARENT_READY;
  words.words[1] = (u64) ended;
  words.words[2] = (u64) live;
  words.words[3] = 0;
  REQUIRE(!ipc_call(channel, &words, 0, &reply) && reply.words[0] == 1, 165);
  return 33;
}
