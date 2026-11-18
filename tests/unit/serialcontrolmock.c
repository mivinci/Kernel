#include <sys/channel.h>
typedef isize (*Mocknotify)(Handle, u64);
static Mocknotify notify;
void
serialmocknotify(Mocknotify function)
{
  notify = function;
}
isize
ipc_notify(Handle target, u64 bits)
{
  return notify(target, bits);
}
