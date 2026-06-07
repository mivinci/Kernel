#include "syscall.h"

static int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return *a - *b;
}

static void readline(char *buf, int max) {
  int i = 0;
  for (;;) {
    while (read(0, &buf[i], 1) == 0)
      yield();
    if (buf[i] == '\n' || buf[i] == '\r') break;
    if (++i >= max - 1) break;
  }
  buf[i] = '\0';
}

void main(void) {
  char buf[64];

  for (;;) {
    write(1, "$ ", 2);

    readline(buf, sizeof(buf));
    if (buf[0] == '\0') continue;

    if (strcmp(buf, "x") == 0)
      exit(0);

    if (strcmp(buf, "hello") == 0) {
      spawn("/bin/hello");
      continue;
    }

    write(1, "not found\n", 10);
  }
}
