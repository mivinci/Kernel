#include "syscall.h"

void main(void) {
  const char *msg = "init: starting sh\n";
  write(1, msg, 18);

  for (;;) {
    int pid = spawn("/bin/sh");
    if (pid >= 0) {
      while (wait(-1) >= 0)
        ;
    }
  }
}
