#include "usr.h"

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

    /* Build absolute path: "/bin/<cmd>", or use as-is if absolute */
    char path[64];
    if (buf[0] == '/') {
      int j;
      for (j = 0; buf[j]; j++) path[j] = buf[j];
      path[j] = '\0';
    } else {
      path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n'; path[4] = '/';
      int i = 5;
      for (int j = 0; buf[j]; j++) path[i++] = buf[j];
      path[i] = '\0';
    }

    spawn(path);
  }
}
