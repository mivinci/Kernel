#include "adopt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      exit(n);                                                                                     \
  } while (0)
int
main(int argc, char **argv, char **envp)
{
  (void) envp;
  Handle pid, parent;
  u64    image;
  Msg    words;
  Ipcmsg reply;
  u32    turns = 0;
  REQUIRE(argc == 1 && !__sys_getid(&pid, &image, &parent) && pid > 1 && image == 1 && parent > 1 &&
              getpid() == pid && getppid() == parent,
          151);
  if (!strcmp(argv[0], "ended") || !strcmp(argv[0], "wake")) {
    words.words[0] = ADOPT_LEAF_READY;
    words.words[1] = (u64) parent;
    words.words[2] = (u64) pid;
    words.words[3] = 0;
    REQUIRE(!ipc_call(bootresource(sysboot(), ADOPT_ENDPOINT), &words, 0, &reply) &&
                reply.words[0] == 1,
            152);
    return 11;
  }
  REQUIRE(!strcmp(argv[0], "live"), 153);
  do {
    REQUIRE(!__sys_yield() && ++turns < 256, 154);
    REQUIRE(!__sys_getid(&pid, &image, &parent) && image == 1 && getppid() == parent, 155);
  } while (parent != 1);
  return 22;
}
