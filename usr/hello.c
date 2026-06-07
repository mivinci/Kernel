#include "syscall.h"

void main(void) {
  const char *msg = "Hello from C!\n";
  write(1, msg, 15);

  for (;;)
    yield();
}
