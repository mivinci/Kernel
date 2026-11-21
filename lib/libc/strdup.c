#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *
duplicate(const char *source, size_t length)
{
  char *copy;
  if (length == SIZE_MAX) {
    errno = ENOMEM;
    return NULL;
  }
  copy = malloc(length + 1);
  if (!copy)
    return NULL;
  memcpy(copy, source, length);
  copy[length] = '\0';
  return copy;
}

char *
strdup(const char *source)
{
  return duplicate(source, strlen(source));
}

char *
strndup(const char *source, size_t limit)
{
  return duplicate(source, strnlen(source, limit));
}
