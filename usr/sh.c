#include "usr.h"

void main(void) {
  char buf[64];
  for (;;) {
    write(1, "$ ", 2);
    int i = 0;
    for (;;) {
      while (read(0, &buf[i], 1) == 0)
        yield();
      if (buf[i] == '\n') break;
      if (++i >= 62) break;
    }
    buf[i] = '\0';
    if (i == 0) continue;

    if (buf[0] == 'x' && buf[1] == '\0') exit(0);

    char path[64];
    if (buf[0] == '/') {
      for (i = 0; buf[i]; i++) path[i] = buf[i];
      path[i] = '\0';
    } else {
      path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n'; path[4] = '/';
      int k = 5;
      for (int j = 0; buf[j]; j++) path[k++] = buf[j];
      path[k] = '\0';
    }

    int pid = spawn(path);
    if (pid < 0) {
      write(1, "sh: not found\n", 14);
    } else {
      wait(pid);
    }
  }
}
