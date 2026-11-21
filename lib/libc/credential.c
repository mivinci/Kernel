#include <unistd.h>
#include "private/syscall.h"
static u32
credential(u32 index)
{
  u32 ids[4];
  /* Current credentials have no POSIX failure result. Query the current
     image each time; inherited libc state is never credential authority. */
  if (__sys_getcred(0, 0, 0, ids))
    __builtin_trap();
  return ids[index];
}
uid_t
getuid(void)
{
  return credential(0);
}
uid_t
geteuid(void)
{
  return credential(1);
}
gid_t
getgid(void)
{
  return credential(2);
}
gid_t
getegid(void)
{
  return credential(3);
}
