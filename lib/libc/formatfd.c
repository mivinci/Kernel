#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "private/format.h"

typedef struct Fdformat Fdformat;
struct Fdformat
{
  int    fd;
  size_t used;
  char   bytes[256];
};

static int
flush(Fdformat *out)
{
  size_t done = 0;
  do {
    ssize_t bytes = write(out->fd, out->bytes + done, out->used - done);
    if (bytes < 0)
      return errno;
    if (!bytes && done != out->used)
      return EIO;
    done += (size_t) bytes;
  } while (done < out->used);
  out->used = 0;
  return 0;
}

static int
emit(void *context, const char *text, char fill, size_t count)
{
  Fdformat *out = context;
  while (count) {
    size_t bytes = MIN(count, sizeof(out->bytes) - out->used);
    if (text) {
      memcpy(out->bytes + out->used, text, bytes);
      text += bytes;
    } else
      memset(out->bytes + out->used, fill, bytes);
    out->used += bytes;
    count -= bytes;
    if (out->used == sizeof(out->bytes)) {
      int error = flush(out);
      if (error)
        return error;
    }
  }
  return 0;
}

int
vdprintf(int fd, const char *format, va_list arguments)
{
  Fdformat out;
  int      result, error;
  out.fd = fd;
  out.used = 0;
  result = __libc_format(NULL, 0, emit, &out, format, arguments);
  if (result < 0)
    return -1;
  if (out.used || !result) {
    error = flush(&out);
    if (error) {
      errno = error;
      return -1;
    }
  }
  return result;
}

int
dprintf(int fd, const char *format, ...)
{
  va_list arguments;
  int     result;
  va_start(arguments, format);
  result = vdprintf(fd, format, arguments);
  va_end(arguments);
  return result;
}
