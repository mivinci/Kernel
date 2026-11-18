#ifndef _SYS_CHANNEL_H
#define _SYS_CHANNEL_H
#include <types.h>
#include <abi/base.h>
#include <abi/ipc.h>
#include <abi/event.h>
typedef struct Ipcmsg Ipcmsg;
struct Ipcmsg
{
  u64    kind;
  Handle source, request;
  u64    words[4], image;
};
STATIC_ASSERT(sizeof(Ipcmsg) == 64, ipc_result_size);
/* Status is native, separate from the service's four business words. Results,
   including an error's outstanding RPC, are always copied before returning. */
isize ipc_call(Handle target, const Msg *message, u64 options, Ipcmsg *out);
isize ipc_send(Handle target, const Msg *message);
isize ipc_recv(u64 mask, u64 options, Ipcmsg *out);
isize ipc_reply(Handle request, const Msg *message);
isize ipc_reply_resource(Handle request, const Resreply *descriptor);
isize ipc_reply_recv(Handle request, const Msg *message, u64 mask, u64 options, Ipcmsg *out);
isize ipc_defer(Handle request);
isize ipc_rqpoll(Handle rpc, u64 options, Ipcmsg *out);
isize ipc_rqwait(Handle rpc, u64 options, Ipcmsg *out);
isize ipc_rqcancel(Handle rpc);
isize ipc_rqack(Handle rpc, u64 options);
isize ipc_rqfinish(Handle request);
isize ipc_rqcheck(Handle request, u64 *state);
isize ipc_notify(Handle target, u64 bits);
#endif
