#include <unistd.h>
#include "private/syscall.h"
static pid_t
identity(u32 parent)
{
  Handle pid, ppid;
  u64    image;
  /* This native query has no failure for the current running image. Do not
     invent a POSIX error result or cache a parent across orphan adoption. */
  if (__sys_getid(&pid, &image, &ppid))
    __builtin_trap();
  return parent ? ppid : pid;
}
pid_t
getpid(void)
{
  return identity(0);
}
pid_t
getppid(void)
{
  return identity(1);
}
