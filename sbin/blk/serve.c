#include "serve.h"
#include "private/syscall.h"
#include <errno.h>

isize
blkservicecall(Blkservice *service, const Ipcmsg *call)
{
  Blktransport *transport = service->transport;
  u64           state;
  isize         error, status;
  Msg           reply;
  if (call->words[0] != BLK_WORD(BLK_HELLO))
    return blktransportcall(transport, call);
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_LIVE) {
    status = blktransportonline(transport)
                 ? sysslothello(transport->links, transport->nlinks, service->observe,
                                service->allocator, service->space, service->arena,
                                BLK_WORD(BLK_HELLO), call)
                 : -ERR_STATE;
    if (!status)
      return 0;
    error = ipc_rqcheck(call->request, &state);
    if (error)
      return error;
  } else
    status = -ERR_INTERRUPTED;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (state & RQ_CHECK_CANCEL)
    status = -EINTR;
  else if (status == -ERR_INVALID)
    status = -EINVAL;
  else if (status == -ERR_BUSY)
    status = -EBUSY;
  else if (status == -ERR_CAPACITY || status == -ERR_LIMIT)
    status = -ENFILE;
  else if (status == -ERR_NOMEM)
    status = -ENOMEM;
  else
    status = -EIO;
  reply = (Msg){{(u64) status, call->words[2], 0, 0}};
  return ipc_reply(call->request, &reply);
}

static isize
advance(Blkservice *service)
{
  Blktransport *transport = service->transport;
  isize         error;
  if (service->pending) {
    Vdone done[VQ_BATCH];
    u32   count, more, causes;
    error = blktransportpoll(transport, done, &count, &more, &causes);
    if (error)
      return error;
    if (!more) {
      error = __sys_irqack(service->irq);
      if (error)
        return error;
      service->pending = 0;
    }
  }
  if (blktransportonline(transport) && transport->queue->configpending) {
    Vblkconfig config;
    error = blkqrefresh(transport->queue, &config);
    if (error && error != -ERR_WOULD_BLOCK)
      return error;
  }
  if (service->retiring) {
    error = blktransportreap(transport);
    if (error && error != -ERR_BUSY && error != -ERR_WOULD_BLOCK)
      return error;
    service->retiring = !!error;
  }
  return 0;
}

isize
blkservestep(Blkservice *service)
{
  Ipcmsg input;
  u64    options;
  isize  error = blktransportpump(service->transport);
  if (error)
    return error;
  /* Finish an already consumed event before taking ownership of a CALL. */
  if (service->pending)
    return advance(service);
  error = advance(service);
  if (error)
    return error;
  /* Once old jobs have drained, a mixed config snapshot needs another
     sample. Its IRQ was already acknowledged, so a new hint is not required. */
  options = blktransportonline(service->transport) && service->transport->queue->configpending &&
                    !service->transport->active
                ? RECV_NONBLOCK
                : 0;
  error = ipc_recv(service->irqevent | EVENT_LIFE | EVENT_RQ_CANCEL, options, &input);
  if (error)
    return error == -ERR_INTERRUPTED || (options && error == -ERR_WOULD_BLOCK) ? 0 : error;
  if (input.kind == IPC_CALL)
    return blkservicecall(service, &input);
  if (input.kind == IPC_EVENT) {
    if ((input.words[0] & service->irqevent) && blktransportonline(service->transport))
      service->pending = 1;
    if (input.words[0] & EVENT_LIFE)
      service->retiring = 1;
    return advance(service);
  }
  return 0;
}

isize
blkservestop(Blkservice *service, u64 now)
{
  service->pending = 0;
  return blktransportrecover(service->transport, now);
}
