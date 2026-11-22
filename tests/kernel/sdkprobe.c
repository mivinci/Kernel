#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
compare(const void *left, const void *right)
{
  int a = *(const int *) left, b = *(const int *) right;
  return (a > b) - (a < b);
}

int
main(int argc, char **argv)
{
  const unsigned char pattern[] = {0x17, 0x00, 0xfe, 0x52};
  unsigned char      *block = malloc(sizeof(pattern));
  int                 values[] = {11, -4, 2, 19, -1};
  int                 allocated = 0, env, length, bytes;
  unsigned int        hash = 2166136261U;
  size_t              i, sent = 0;
  char                text[32], line[160];
  if (block) {
    memcpy(block, pattern, sizeof(pattern));
    allocated = !memcmp(block, pattern, sizeof(pattern));
    free(block);
  }
  env = environ && environ[0] && environ[0][0];
  qsort(values, sizeof(values) / sizeof(values[0]), sizeof(values[0]), compare);
  length = snprintf(text, sizeof(text), "%s:%d:%04u", "fmt", -7, 23U);
  if (length < 0 || (size_t) length >= sizeof(text))
    return EXIT_FAILURE;
  length = snprintf(line, sizeof(line), "sdk-a1 argc=%d arg1=%s env=%d alloc=%d text=%s sort=%d,%d",
                    argc, argc > 1 ? argv[1] : "", env, allocated, text, values[0], values[4]);
  if (length < 0 || (size_t) length >= sizeof(line))
    return EXIT_FAILURE;
  for (i = 0; i < (size_t) length; i++)
    hash = (hash ^ (unsigned char) line[i]) * 16777619U;
  bytes = snprintf(line + length, sizeof(line) - (size_t) length, " hash=%08x\n", hash);
  if (bytes < 0 || (size_t) bytes >= sizeof(line) - (size_t) length)
    return EXIT_FAILURE;
  length += bytes;
  while (sent < (size_t) length) {
    ssize_t count = write(1, line + sent, (size_t) length - sent);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return EXIT_FAILURE;
    sent += (size_t) count;
  }
  return argc == 2 && !strcmp(argv[1], "probe") && env && allocated &&
                 !strcmp(text, "fmt:-7:0023") && values[0] == -4 && values[1] == -1 &&
                 values[2] == 2 && values[3] == 11 && values[4] == 19
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
