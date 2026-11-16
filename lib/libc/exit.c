#include "private/libc.h"
#include "private/syscall.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#define EXIT_SLOTS 32
static Exitfn callbacks[EXIT_SLOTS], finish;
static u32    count, exiting;
void
libcexitinit(Exitfn finalizer)
{
  count = exiting = 0;
  finish = finalizer;
}
int
atexit(void (*function)(void))
{
  if (!function || exiting || count == EXIT_SLOTS) {
    errno = !function ? EINVAL : exiting ? EBUSY : ENOMEM;
    return -1;
  }
  callbacks[count++] = function;
  return 0;
}
NORETURN void
_exit(int status)
{
  __sys_exit((u64) (u8) status);
}
NORETURN void
exit(int status)
{
  Exitfn callback;
  if (exiting)
    _exit(status);
  exiting = 1;
  while (count) {
    callback = callbacks[--count];
    callbacks[count] = NULL;
    callback();
  }
  if (finish) {
    callback = finish;
    finish = NULL;
    callback();
  }
  _exit(status);
}
