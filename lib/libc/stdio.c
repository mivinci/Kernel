#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "private/format.h"
#include "private/libc.h"

enum
{
  Stdiounset,
  Stdionone,
  Stdioline,
  Stdiofull
};

struct Stdiofile
{
  int    fd, mode, failed, ended, pushed;
  size_t used, position;
  char   buffer[256];
};

static FILE standardin = {0, Stdiounset, 0, 0, EOF, 0, 0, {0}};
static FILE standardout = {1, Stdiounset, 0, 0, EOF, 0, 0, {0}};
static FILE standarderr = {2, Stdionone, 0, 0, EOF, 0, 0, {0}};
FILE *const stdin = &standardin;
FILE *const stdout = &standardout;
FILE *const stderr = &standarderr;

static size_t
writebytes(FILE *stream, const char *data, size_t count)
{
  size_t done = 0;
  while (done < count) {
    ssize_t bytes = write(stream->fd, data + done, count - done);
    if (bytes <= 0) {
      if (!bytes)
        errno = EIO;
      stream->failed = 1;
      break;
    }
    done += (size_t) bytes;
  }
  return done;
}

int
fflush(FILE *stream)
{
  size_t done, count;
  if (!stream) {
    int result = fflush(stdout), saved = errno;
    if (fflush(stdin) == EOF && result != EOF)
      return EOF;
    errno = saved;
    return result;
  }
  if (stream == stdin) {
    int saved = errno;
    count = stream->used - stream->position + (stream->pushed != EOF);
    if (!count)
      return 0;
    if (lseek(stream->fd, -(off_t) count, SEEK_CUR) < 0) {
      if (errno != ESPIPE) {
        stream->failed = 1;
        return EOF;
      }
      /* A pipe or terminal cannot put read-ahead back into the descriptor. */
      stream->pushed = EOF;
      errno = saved;
      return 0;
    }
    stream->used = stream->position = 0;
    stream->pushed = EOF;
    return 0;
  }
  count = stream->used;
  done = writebytes(stream, stream->buffer, count);
  /* After a failed flush, retain only the part the descriptor did not accept. */
  stream->used = count - done;
  if (done && stream->used)
    memmove(stream->buffer, stream->buffer + done, stream->used);
  return done == count ? 0 : EOF;
}

static void
stdioexit(void)
{
  fflush(stdout);
}

int
ferror(FILE *stream)
{
  return stream->failed;
}

int
feof(FILE *stream)
{
  return stream->ended;
}

void
clearerr(FILE *stream)
{
  stream->failed = stream->ended = 0;
}

static int
readable(FILE *stream)
{
  if (stream == stdin)
    return 1;
  stream->failed = 1;
  errno = EBADF;
  return 0;
}

static int
refill(FILE *stream)
{
  ssize_t bytes;
  if (stream->ended)
    return 0;
  if (stdout->mode == Stdioline) {
    int saved = errno;
    fflush(stdout);
    errno = saved;
  }
  bytes = read(stream->fd, stream->buffer, sizeof(stream->buffer));
  if (bytes <= 0) {
    if (bytes)
      stream->failed = 1;
    else
      stream->ended = 1;
    return 0;
  }
  stream->position = 0;
  stream->used = (size_t) bytes;
  return 1;
}

static size_t
input(FILE *stream, char *data, size_t count, int *lineerror)
{
  size_t done = 0;
  if (!count || !readable(stream))
    return 0;
  if (stream->pushed != EOF) {
    data[done++] = (char) stream->pushed;
    stream->pushed = EOF;
    if (lineerror && data[0] == '\n')
      return done;
  }
  while (done < count) {
    const char *start, *newline;
    size_t      bytes;
    if (stream->position == stream->used && !refill(stream)) {
      if (lineerror)
        *lineerror = !stream->ended;
      break;
    }
    start = stream->buffer + stream->position;
    bytes = MIN(count - done, stream->used - stream->position);
    newline = lineerror ? memchr(start, '\n', bytes) : NULL;
    if (newline)
      bytes = (size_t) (newline - start) + 1;
    memcpy(data + done, start, bytes);
    stream->position += bytes;
    done += bytes;
    if (newline)
      break;
  }
  return done;
}

size_t
fread(void *data, size_t size, size_t count, FILE *stream)
{
  if (!size || !count)
    return 0;
  if (count > SIZE_MAX / size) {
    stream->failed = 1;
    errno = EOVERFLOW;
    return 0;
  }
  return input(stream, data, size * count, 0) / size;
}

char *
fgets(char *text, int size, FILE *stream)
{
  size_t count;
  int    error = 0;
  if (size <= 0) {
    errno = EINVAL;
    return NULL;
  }
  if (size == 1) {
    text[0] = 0;
    return text;
  }
  count = input(stream, text, (size_t) size - 1, &error);
  if (!count || error)
    return NULL;
  text[count] = 0;
  return text;
}

int
fgetc(FILE *stream)
{
  unsigned char byte;
  return input(stream, (char *) &byte, 1, 0) ? byte : EOF;
}

int
getc(FILE *stream)
{
  return fgetc(stream);
}

int
getchar(void)
{
  return fgetc(stdin);
}

int
ungetc(int byte, FILE *stream)
{
  if (byte == EOF || !readable(stream) || stream->pushed != EOF)
    return EOF;
  stream->ended = 0;
  return stream->pushed = (unsigned char) byte;
}

static size_t
output(FILE *stream, const char *data, size_t count)
{
  size_t done = 0;
  if (!count)
    return 0;
  if (stream == stdin) {
    stream->failed = 1;
    errno = EBADF;
    return 0;
  }
  if (stream->mode == Stdiounset) {
    int saved = errno;
    stream->mode = isatty(stream->fd) ? Stdioline : Stdiofull;
    libcexitflush(stdioexit);
    errno = saved;
  }
  if (stream->mode == Stdionone)
    return writebytes(stream, data, count);
  while (done < count) {
    size_t      old = stream->used;
    size_t      bytes = MIN(count - done, sizeof(stream->buffer) - old);
    const char *newline = stream->mode == Stdioline ? memchr(data + done, '\n', bytes) : NULL;
    if (newline)
      bytes = (size_t) (newline - (data + done)) + 1;
    memcpy(stream->buffer + old, data + done, bytes);
    stream->used += bytes;
    if ((newline || stream->used == sizeof(stream->buffer)) && fflush(stream) == EOF) {
      size_t sent = old + bytes - stream->used;
      /* A failed write must not queue bytes the caller is told to retry. */
      stream->used = sent < old ? old - sent : 0;
      return done + (sent > old ? sent - old : 0);
    }
    done += bytes;
  }
  return done;
}

size_t
fwrite(const void *data, size_t size, size_t count, FILE *stream)
{
  if (!size || !count)
    return 0;
  if (count > SIZE_MAX / size) {
    stream->failed = 1;
    errno = EOVERFLOW;
    return 0;
  }
  return output(stream, data, size * count) / size;
}

int
fputs(const char *text, FILE *stream)
{
  size_t count = strlen(text);
  return output(stream, text, count) == count ? 0 : EOF;
}

int
fputc(int byte, FILE *stream)
{
  unsigned char value = (unsigned char) byte;
  return output(stream, (const char *) &value, 1) == 1 ? value : EOF;
}

int
putc(int byte, FILE *stream)
{
  return fputc(byte, stream);
}

static int
emit(void *context, const char *text, char fill, size_t count)
{
  char repeated[64];
  if (text)
    return output(context, text, count) == count ? 0 : errno;
  memset(repeated, fill, sizeof(repeated));
  while (count) {
    size_t bytes = MIN(count, sizeof(repeated));
    if (output(context, repeated, bytes) != bytes)
      return errno;
    count -= bytes;
  }
  return 0;
}

int
vfprintf(FILE *stream, const char *format, va_list arguments)
{
  return __libc_format(NULL, 0, emit, stream, format, arguments);
}

int
fprintf(FILE *stream, const char *format, ...)
{
  va_list arguments;
  int     result;
  va_start(arguments, format);
  result = vfprintf(stream, format, arguments);
  va_end(arguments);
  return result;
}

int
vprintf(const char *format, va_list arguments)
{
  return vfprintf(stdout, format, arguments);
}

int
printf(const char *format, ...)
{
  va_list arguments;
  int     result;
  va_start(arguments, format);
  result = vprintf(format, arguments);
  va_end(arguments);
  return result;
}
