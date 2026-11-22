#include "serve.h"
#include "private/syscall.h"
#include <errno.h>

static isize
reject(const Ipcmsg *call, isize error)
{
  Msg reply = {{(u64) error, call->words[2], 0, 0}};
  return ipc_reply(call->request, &reply);
}

static isize
hello(Fsservice *fs, const Ipcmsg *call)
{
  u64   state;
  isize error, status;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (state & RQ_CHECK_CANCEL)
    return reject(call, -EINTR);
  status =
      sysfilehello(fs->links, fs->nlinks, fs->observe, fs->allocator, fs->space, fs->arena, call);
  if (!status)
    return 0;
  /* Resource delivery can lose the race to client exit. HELLO rolls back the
     unpublished link; the original received request still needs finishing. */
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  switch (status) {
  case -ERR_INVALID:
    return reject(call, -EINVAL);
  case -ERR_BUSY:
    return reject(call, -EBUSY);
  case -ERR_CAPACITY:
  case -ERR_LIMIT:
    return reject(call, -ENFILE);
  case -ERR_NOMEM:
    return reject(call, -ENOMEM);
  default:
    return reject(call, -EIO);
  }
}

static isize
drain(void *context, Filelink *link)
{
  Fsservice *fs = context;
  if (fsloadretire(fs->loads, link) || fsopenbusy(fs->open, link) ||
      fsreadbusy(fs->pipes.reads, link)) {
    fs->retiring = 1;
    return -ERR_WOULD_BLOCK;
  }
  return pipedrain(&fs->pipes, link);
}

isize
fsstep(Fsservice *fs)
{
  Ipcmsg     input;
  isize      error;
  u64        deadline, generation;
  u32        again = 0;
  Fsreclaim *reclaim = fs->reclaim;
  if (fs->fetch && fs->log && fs->fetch->blk == fs->log->blk)
    fs->fetch->journal = fs->log->journal;
  if (fs->log && fs->log->ticket.serial) {
    error = blklogpoll(fs->log);
    if (error && error != -ERR_WOULD_BLOCK)
      return error;
  }
  if (fs->fetch && (error = blkfetchpoll(fs->fetch)))
    return error;
  error = fsloadpump(fs->loads);
  if (error)
    return error;
  error = fsopenpump(fs->open);
  if (error)
    return error;
  again = fs->open && fs->open->again;
  error = fsreadpump(fs->pipes.reads, fs->pipes.regular);
  if (error)
    return error;
  if (fs->retiring) {
    fs->retiring = 0;
    error = sysfilelinksweep(fs->links, fs->nlinks, drain, fs);
    if (error != -ERR_WOULD_BLOCK)
      return error;
  }
  error = pipepump(&fs->pipes);
  if (error)
    return error;
  if (reclaim && reclaim->work) {
    if (!reclaim->work->txid && reclaim->scan && reclaim->scan->left) {
      error = reclaimscanstep(reclaim->scan, reclaim->work, reclaim->candidate, reclaim->context);
      if (error < 0)
        return error;
    }
    if (reclaim->work->txid) {
      error = reclaimstep(reclaim->work, reclaim->submit, reclaim->context);
      if (error < 0)
        return error;
      again |= error == 1;
    } else
      again |= reclaim->scan && reclaim->scan->left;
  }
  deadline = fs->loads ? loaddeadline(fs->loads->table) : 0;
  if (deadline != fs->loadalarm) {
    error = deadline ? __sys_alarmset(deadline, &generation) : __sys_alarmcancel(&generation);
    if (error)
      return error;
    fs->loadalarm = deadline;
  }
  error = ipc_recv(
      EVENT_LIFE | EVENT_RQ_CANCEL | (deadline ? EVENT_TIMER : 0) |
          (fs->open ? fs->open->events : 0) | (fs->fetch || fs->log ? EVENT_RQ_DONE : 0) |
          (fs->loads ? fs->loads->events : 0) | (fs->pipes.reads ? fs->pipes.reads->events : 0) |
          (fs->reclaim ? fs->reclaim->events : 0),
      again ? RECV_NONBLOCK : 0, &input);
  if (error)
    return error == -ERR_INTERRUPTED || (again && error == -ERR_WOULD_BLOCK) ? 0 : error;
  if (input.kind == IPC_EVENT) {
    if (input.words[0] & EVENT_LIFE) {
      error = sysfilelinksweep(fs->links, fs->nlinks, drain, fs);
      return error == -ERR_WOULD_BLOCK ? 0 : error;
    }
    return 0;
  }
  if (input.kind != IPC_CALL)
    return 0;
  switch (input.words[0]) {
  case FS_HELLO:
    return hello(fs, &input);
  case FS_LOAD:
    return fsloadstart(fs->loads, fs->links, fs->nlinks, &input);
  case FS_OPEN:
  case FS_PATH:
    return fsopenstart(fs->open, fs->links, fs->nlinks, &input);
  case FS_PIPE:
  case FS_CLOSE:
  case FS_DUPREF:
  case FS_SEEK:
  case FS_STAT:
  case FS_SYNC:
  case FS_TTYCTL:
  case FS_POLL_QUERY:
  case FS_POLL_WAIT:
  case FS_READ:
  case FS_WRITE:
    return pipedispatch(&fs->pipes, fs->links, fs->nlinks, &input);
  default:
    return reject(&input, -ENOSYS);
  }
}
