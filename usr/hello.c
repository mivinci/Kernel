#include "usr.h"

void main(void) {
  const char *msg = "Hello from C!\n";
  write(1, msg, 15);
  exit(0);
}
