#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
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

enum
{
  Stdioread = 1,
  Stdiowrite = 2
};

struct Stdiofile
{
  int    fd, mode, failed, ended, pushed;
  size_t used, position;
  char   buffer[256];
  int    access, reading, append;
  FILE  *next;
};

static FILE  standardin = {0, Stdiounset, 0, 0, EOF, 0, 0, {0}, Stdioread, 1, 0, NULL};
static FILE  standardout = {1, Stdiounset, 0, 0, EOF, 0, 0, {0}, Stdiowrite, 0, 0, NULL};
static FILE  standarderr = {2, Stdionone, 0, 0, EOF, 0, 0, {0}, Stdiowrite, 0, 0, NULL};
static FILE *opened;
FILE *const  stdin = &standardin;
FILE *const  stdout = &standardout;
FILE *const  stderr = &standarderr;

static void stdioexit(void);

FILE *
fopen(const char *path, const char *mode)
{
  FILE *stream;
  int   access, flags, binary = 0, update = 0, exclusive = 0, cloexec = 0, fd, saved;
  char  initial = *mode++;
  switch (initial) {
  case 'r':
    flags = 0;
    access = Stdioread;
    break;
  case 'w':
    flags = O_CREAT | O_TRUNC;
    access = Stdiowrite;
    break;
  case 'a':
    flags = O_CREAT | O_APPEND;
    access = Stdiowrite;
    break;
  default:
    goto invalid;
  }
  for (; *mode; mode++) {
    if (*mode == 'b' && !binary)
      binary = 1;
    else if (*mode == '+' && !update)
      update = 1;
    else if (*mode == 'x' && initial != 'r' && !exclusive)
      exclusive = 1;
    else if (*mode == 'e' && !cloexec)
      cloexec = 1;
    else
      goto invalid;
  }
  if (exclusive)
    flags |= O_EXCL;
  if (cloexec)
    flags |= O_CLOEXEC;
  if (update)
    access = Stdioread | Stdiowrite;
  flags |= update ? O_RDWR : initial == 'r' ? O_RDONLY : O_WRONLY;
  stream = calloc(1, sizeof(*stream));
  if (!stream)
    return NULL;
  fd = open(path, flags, (mode_t) 0666);
  if (fd < 0) {
    saved = errno;
    free(stream);
    errno = saved;
    return NULL;
  }
  stream->fd = fd;
  stream->access = access;
  stream->reading = initial == 'r';
  stream->append = initial == 'a';
  stream->pushed = EOF;
  stream->next = opened;
  opened = stream;
  libcexitflush(stdioexit);
  return stream;
invalid:
  errno = EINVAL;
  return NULL;
}

int
fileno(FILE *stream)
{
  if (stream->fd < 0)
    errno = EBADF;
  return stream->fd;
}

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
    FILE *item;
    int   error = 0;
    if (stdout->fd >= 0 && fflush(stdout) == EOF)
      error = errno;
    if (stdin->fd >= 0 && fflush(stdin) == EOF && !error)
      error = errno;
    for (item = opened; item; item = item->next)
      if (fflush(item) == EOF && !error)
        error = errno;
    if (error)
      errno = error;
    return error ? EOF : 0;
  }
  if (stream->fd < 0) {
    stream->failed = 1;
    errno = EBADF;
    return EOF;
  }
  if (stream->reading) {
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

static off_t
unread(const FILE *stream)
{
  return (off_t) (stream->used - stream->position + (stream->pushed != EOF));
}

int
fseeko(FILE *stream, off_t offset, int whence)
{
  off_t ahead;
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    errno = EINVAL;
    return -1;
  }
  if (stream->reading && whence == SEEK_CUR) {
    ahead = unread(stream);
    if (offset < INT64_MIN + ahead) {
      errno = EOVERFLOW;
      return -1;
    }
    offset -= ahead;
  } else if (!stream->reading && stream->used && fflush(stream) == EOF)
    return -1;
  if (lseek(stream->fd, offset, whence) < 0)
    return -1;
  stream->used = stream->position = 0;
  stream->ended = 0;
  stream->pushed = EOF;
  return 0;
}

off_t
ftello(FILE *stream)
{
  off_t position, change;
  /* The server chooses the append position at write commit. */
  if (!stream->reading && stream->append && stream->used && fflush(stream) == EOF)
    return -1;
  position = lseek(stream->fd, 0, SEEK_CUR);
  if (position < 0)
    return -1;
  change = stream->reading ? -unread(stream) : (off_t) stream->used;
  if (change < -position || (change > 0 && position > INT64_MAX - change)) {
    errno = EOVERFLOW;
    return -1;
  }
  return position + change;
}

int
fseek(FILE *stream, long offset, int whence)
{
  return fseeko(stream, (off_t) offset, whence);
}

long
ftell(FILE *stream)
{
  int   saved = errno;
  off_t position = ftello(stream);
  if (position >= 0)
    errno = saved;
  return (long) position;
}

void
rewind(FILE *stream)
{
  (void) fseeko(stream, 0, SEEK_SET);
  stream->failed = 0;
}

static void
stdioexit(void)
{
  FILE *stream;
  if (stdout->fd >= 0)
    fflush(stdout);
  for (stream = opened; stream; stream = stream->next)
    if (!stream->reading)
      fflush(stream);
}

int
fclose(FILE *stream)
{
  FILE **link;
  int    error = 0;
  if (stream->fd < 0) {
    errno = EBADF;
    return EOF;
  }
  if (fflush(stream) == EOF)
    error = errno;
  if (close(stream->fd) && !error)
    error = errno;
  stream->fd = -1;
  for (link = &opened; *link; link = &(*link)->next)
    if (*link == stream) {
      *link = stream->next;
      free(stream);
      break;
    }
  if (error)
    errno = error;
  return error ? EOF : 0;
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
  if (stream->fd >= 0 && (stream->access & Stdioread)) {
    if (!stream->reading && fflush(stream) == EOF)
      return 0;
    stream->reading = 1;
    return 1;
  }
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
  if (stream->fd < 0 || !(stream->access & Stdiowrite)) {
    stream->failed = 1;
    errno = EBADF;
    return 0;
  }
  if (stream->reading) {
    if (fflush(stream) == EOF)
      return 0;
    stream->used = stream->position = 0;
    stream->pushed = EOF;
    stream->ended = stream->reading = 0;
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

int
putchar(int byte)
{
  return fputc(byte, stdout);
}

int
puts(const char *text)
{
  if (fputs(text, stdout) == EOF || putchar('\n') == EOF)
    return EOF;
  return 0;
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
