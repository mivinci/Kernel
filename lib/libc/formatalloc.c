#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int
vasprintf(char **output, const char *format, va_list arguments)
{
  va_list copy;
  char   *buffer;
  size_t  size;
  int     length, written, error, saved = errno;
  __builtin_va_copy(copy, arguments);
  length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0)
    return -1;
  size = (size_t) length + 1;
  buffer = malloc(size);
  if (!buffer) {
    errno = ENOMEM;
    return -1;
  }
  written = vsnprintf(buffer, size, format, arguments);
  if (written < 0 || written > length) {
    error = written < 0 ? errno : EOVERFLOW;
    free(buffer);
    errno = error;
    return -1;
  }
  *output = buffer;
  errno = saved;
  return written;
}

int
asprintf(char **output, const char *format, ...)
{
  va_list arguments;
  int     result;
  va_start(arguments, format);
  result = vasprintf(output, format, arguments);
  va_end(arguments);
  return result;
}
