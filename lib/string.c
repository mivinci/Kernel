#include <libc.h>

size_t strlen(const char *p) {
  size_t n = 0;
  while (*p++) n++;
  return n;
}
