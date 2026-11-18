#include <sys/channel.h>
#include "private/syscall.h"
#include <abi/syscall.h>
STATIC_ASSERT(sizeof(Ipcmsg) == sizeof(Sysret), ipc_raw_result_size);
STATIC_ASSERT(offsetof(Ipcmsg, image) == offsetof(Sysret, image), ipc_raw_result_image);
static isize
message(u64 number, Handle target, const Msg *words, u64 mask, u64 options, Ipcmsg *out)
{
  u64    args[7];
  Sysret result;
  u32    i;
  args[0] = (u64) target;
  for (i = 0; i < 4; i++)
    args[i + 1] = words ? words->words[i] : 0;
  if (number == SYS_RECV)
    args[1] = mask;
  args[5] = options;
  args[6] = number == SYS_REPLY_RECV ? mask : 0;
  return __sys_enter(number, args, out ? (void *) out : (void *) &result);
}
isize
ipc_call(Handle target, const Msg *words, u64 options, Ipcmsg *out)
{
  if (!out)
    return -ERR_INVALID;
  return message(SYS_CALL, target, words, 0, options, out);
}
isize
ipc_send(Handle target, const Msg *words)
{
  return message(SYS_SEND, target, words, 0, 0, NULL);
}
isize
ipc_recv(u64 mask, u64 options, Ipcmsg *out)
{
  if (!out)
    return -ERR_INVALID;
  return message(SYS_RECV, 0, NULL, mask, options, out);
}
isize
ipc_reply(Handle request, const Msg *words)
{
  return message(SYS_REPLY, request, words, 0, 0, NULL);
}
isize
ipc_reply_resource(Handle request, const Resreply *descriptor)
{
  const u64 args[7] = {(u64) request, (u64) descriptor, 0, 0, 0, REPLY_RESOURCE, 0};
  Sysret    result;
  if (!descriptor)
    return -ERR_INVALID;
  return __sys_enter(SYS_REPLY, args, &result);
}
isize
ipc_reply_recv(Handle request, const Msg *words, u64 mask, u64 options, Ipcmsg *out)
{
  if (!out)
    return -ERR_INVALID;
  return message(SYS_REPLY_RECV, request, words, mask, options, out);
}
static isize
request(u64 number, Handle handle, u64 options, Ipcmsg *out, u64 *state)
{
  const u64 args[7] = {(u64) handle, options, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(number, args, out ? (void *) out : (void *) &result);
  if (state)
    *state = result.values[0];
  return status;
}
isize
ipc_defer(Handle handle)
{
  return request(SYS_DEFER, handle, 0, NULL, NULL);
}
isize
ipc_rqpoll(Handle handle, u64 options, Ipcmsg *out)
{
  if (!out)
    return -ERR_INVALID;
  return request(SYS_RQPOLL, handle, options, out, NULL);
}
isize
ipc_rqwait(Handle handle, u64 options, Ipcmsg *out)
{
  if (!out)
    return -ERR_INVALID;
  return request(SYS_RQWAIT, handle, options, out, NULL);
}
isize
ipc_rqcancel(Handle handle)
{
  return request(SYS_RQCANCEL, handle, 0, NULL, NULL);
}
isize
ipc_rqack(Handle handle, u64 options)
{
  return request(SYS_RQACK, handle, options, NULL, NULL);
}
isize
ipc_rqfinish(Handle handle)
{
  return request(SYS_RQFINISH, handle, 0, NULL, NULL);
}
isize
ipc_rqcheck(Handle handle, u64 *state)
{
  if (!state)
    return -ERR_INVALID;
  return request(SYS_RQCHECK, handle, 0, NULL, state);
}
isize
ipc_notify(Handle handle, u64 bits)
{
  return request(SYS_NOTIFY, handle, bits, NULL, NULL);
}
