#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static char **owned;
static char  *empty[1];
static int
invalid(const char *name)
{
  return !name || !*name || strchr(name, '=') != NULL;
}
static int
matches(const char *entry, const char *name, size_t length)
{
  return !strncmp(entry, name, length) && entry[length] == '=';
}
static int
nomemory(void)
{
  errno = ENOMEM;
  return -1;
}
int
setenv(const char *name, const char *value, int overwrite)
{
  char **previous = environ, **next, **old;
  char  *text;
  size_t length, valuebytes, count = 0, bytes = 0, total, i, at = 0, amount;
  u32    inserted = 0;
  if (invalid(name) || !value) {
    errno = EINVAL;
    return -1;
  }
  length = strlen(name);
  if (!overwrite && previous)
    for (i = 0; previous[i]; i++)
      if (matches(previous[i], name, length))
        return 0;
  valuebytes = strlen(value);
  if (valuebytes == SIZE_MAX || length > SIZE_MAX - valuebytes - 1)
    return nomemory();
  bytes = length + valuebytes + 1;
  if (bytes == SIZE_MAX)
    return nomemory();
  bytes++; /* '=' and the final NUL. */
  if (previous)
    for (i = 0; previous[i]; i++) {
      if (matches(previous[i], name, length))
        continue;
      amount = strlen(previous[i]);
      if (amount == SIZE_MAX || amount + 1 > SIZE_MAX - bytes ||
          count >= SIZE_MAX / sizeof(char *) - 2)
        return nomemory();
      bytes += amount + 1;
      count++;
    }
  total = (count + 2) * sizeof(char *);
  if (bytes > SIZE_MAX - total)
    return nomemory();
  next = malloc(total + bytes);
  if (!next)
    return nomemory();
  text = (char *) next + total;
  /* Copy the complete new snapshot before freeing anything: name/value or
     a replacement environ may still borrow strings in the previous block. */
  if (previous)
    for (i = 0; previous[i]; i++) {
      if (matches(previous[i], name, length)) {
        if (inserted)
          continue;
        next[at++] = text;
        memcpy(text, name, length);
        text[length] = '=';
        memcpy(text + length + 1, value, valuebytes + 1);
        text += length + valuebytes + 2;
        inserted = 1;
      } else {
        amount = strlen(previous[i]) + 1;
        next[at++] = text;
        memcpy(text, previous[i], amount);
        text += amount;
      }
    }
  if (!inserted) {
    next[at++] = text;
    memcpy(text, name, length);
    text[length] = '=';
    memcpy(text + length + 1, value, valuebytes + 1);
  }
  next[at] = NULL;
  old = owned;
  owned = next;
  environ = next;
  free(old);
  return 0;
}
int
unsetenv(const char *name)
{
  size_t length, first = 0, read, write;
  if (invalid(name)) {
    errno = EINVAL;
    return -1;
  }
  if (!environ)
    return 0;
  length = strlen(name);
  while (environ[first] && !matches(environ[first], name, length))
    first++;
  if (!environ[first])
    return 0;
  write = first;
  for (read = first + 1; environ[read]; read++)
    if (!matches(environ[read], name, length))
      environ[write++] = environ[read];
  environ[write] = NULL;
  if (!write) {
    char **old = owned;
    owned = NULL;
    environ = empty;
    free(old);
  }
  return 0;
}
