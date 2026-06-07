#include "usr.h"

void main(void) {
  char buf[64];

  for (;;) {
    int n = read(0, buf, sizeof(buf));
    if (n > 0) write(1, buf, n);
    else yield();
  }
}
