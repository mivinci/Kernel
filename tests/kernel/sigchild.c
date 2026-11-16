#include <stdlib.h>
#include <string.h>
#include "private/syscall.h"
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      exit(n);                                                                                     \
  } while (0)
int
main(int argc, char **argv)
{
  Sigact term, chld;
  u64    mask, pending;
  u32    reset, ignored;
  REQUIRE(argc == 1, 151);
  reset = !strcmp(argv[0], "reset");
  ignored = !strcmp(argv[0], "auto") || !strcmp(argv[0], "again");
  REQUIRE(reset || ignored || !strcmp(argv[0], "keep"), 152);
  REQUIRE(!__sys_sigaction(SIGNAL_TERM, NULL, &term) && !__sys_sigaction(SIGNAL_CHLD, NULL, &chld),
          153);
  REQUIRE(term.kind == (reset ? SIGNAL_DEFAULT : SIGNAL_IGNORE) && !term.handler &&
              !term.trampoline && !term.mask && !term.flags,
          154);
  REQUIRE(chld.kind == (ignored ? SIGNAL_IGNORE : SIGNAL_DEFAULT) && !chld.flags && !chld.mask,
          155);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
              mask == ((u64) 1 << ((reset ? SIGNAL_USR2 : SIGNAL_USR1) - 1)),
          156);
  REQUIRE(!__sys_sigpending(&pending) && !pending, 157);
  return 31;
}
