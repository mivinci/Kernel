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

static void
forkdrop(Fsservice *fs, Fsfork *record)
{
  fsfiledropsnapshot(fs->pipes.regular, record->holds, record->manifest.count);
  record->manifest.count = 0;
  record->context = 0;
}

static isize
forksweep(Fsservice *fs, const Filelink *link, u64 *deadline)
{
  u32   i;
  u64   now = 0;
  isize error;
  if (!fs->forks)
    return 0;
  if (!link && (error = sysfilenow(&now)))
    return error;
  for (i = 0; i < fs->forks->count; i++) {
    Fsfork             *record = &fs->forks->records[i];
    const Execmanifest *m = &record->manifest;
    if (!m->count)
      continue;
    if (link) {
      if (m->pid == link->watch.owner.pid && m->source_image == link->watch.owner.image &&
          m->connid == link->watch.connid)
        forkdrop(fs, record);
    } else if (m->deadline <= now)
      forkdrop(fs, record);
    else if (!*deadline || m->deadline < *deadline)
      *deadline = m->deadline;
  }
  return 0;
}

static isize
forkcapture(Fsservice *fs, const Ipcmsg *call, Filelink *link, const Fileslot *slot,
            Fsfork **captured)
{
  Fsfork          *record;
  const Fscontext *context;
  Connpeer         self;
  Handle           parent;
  u64              now;
  u32              i;
  isize            error;
  context = fscontextfind(fs->open->contexts, call->source, call->image);
  if (!context)
    return -ESTALE;
  for (i = 0; i < fs->forks->count && fs->forks->records[i].manifest.count; i++) {
  }
  if (i == fs->forks->count)
    return -EAGAIN;
  if (fs->forks->serial == UINT64_MAX)
    return -EOVERFLOW;
  record = &fs->forks->records[i];
  if (__sys_getid(&self.pid, &self.image, &parent) || sysfilenow(&now))
    return -EIO;
  error = sysforksnapshot(
      &record->manifest, &link->watch, call, slot,
      (const void *) (link->address + FS_DATA_OFFSET + call->words[3] * FS_SLOT_BYTES), self, now,
      fsfilerefcheck, fs->pipes.regular, fs->forks->serial + 1);
  if (!error) {
    error = fsfileholdsnapshot(fs->pipes.regular, &record->manifest, record->holds, BOOT_FD_COUNT);
    if (!error) {
      fs->forks->serial++;
      record->context = context->version;
      *captured = record;
      return 0;
    }
    record->manifest.count = 0;
  }
  switch (error) {
  case -ERR_INVALID:
    return -EINVAL;
  case -ERR_STALE:
    return -ESTALE;
  case -ERR_TIMEDOUT:
    return -ETIMEDOUT;
  case -ERR_OVERFLOW:
    return -EOVERFLOW;
  default:
    return -EIO;
  }
}

static isize
forksnapshot(Fsservice *fs, const Ipcmsg *call)
{
  Filelink *link = NULL;
  Fsfork   *record = NULL;
  Fileslot  slot;
  Msg       reply = {{(u64) -ENOSYS, call->words[2], 0, 0}};
  u64       state, now;
  isize     error;
  if (fs->forks && fs->open && fs->pipes.regular) {
    error = sysfilelinkaccept(fs->links, fs->nlinks, call, &slot);
    if (!error) {
      link = &fs->links[call->words[1] & 65535];
      reply.words[0] = (u64) forkcapture(fs, call, link, &slot, &record);
    } else
      reply.words[0] = (u64) (error == -ERR_INVALID ? -EINVAL
                              : error == -ERR_BUSY  ? -EBUSY
                                                    : -ESTALE);
  }
  error = ipc_rqcheck(call->request, &state);
  if (!error && state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
  } else if (!error) {
    if (state & RQ_CHECK_CANCEL)
      reply.words[0] = (u64) -EINTR;
    if (record && state == RQ_CHECK_LIVE) {
      if (sysfilenow(&now))
        reply.words[0] = (u64) -EIO;
      else if (record->manifest.deadline <= now)
        reply.words[0] = (u64) -ETIMEDOUT;
      else
        reply.words[2] = record->manifest.snapshot;
    }
    error = ipc_reply(call->request, &reply);
  }
  if (record && (error || !reply.words[2]))
    forkdrop(fs, record);
  return error || !link ? error : sysfilefinish(&link->watch.active, call->request);
}

static isize
drain(void *context, Filelink *link)
{
  Fsservice *fs = context;
  isize      error;
  forksweep(fs, link, NULL);
  if (fsloadretire(fs->loads, link) || fsopenbusy(fs->open, link) ||
      fsreadbusy(fs->pipes.reads, link)) {
    fs->retiring = 1;
    return -ERR_WOULD_BLOCK;
  }
  error = pipedrain(&fs->pipes, link);
  if (error || !fs->open || !fs->open->contexts)
    return error;
  /* Drop only the ended image's namespace after its jobs have drained. */
  error = fscontextremove(fs->open->contexts, link->watch.owner.pid, link->watch.owner.image);
  return error == -ESTALE ? 0 : error;
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
  error = forksweep(fs, NULL, &deadline);
  if (error)
    return error;
  if (deadline != fs->alarm) {
    error = deadline ? __sys_alarmset(deadline, &generation) : __sys_alarmcancel(&generation);
    if (error)
      return error;
    fs->alarm = deadline;
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
  case FS_FORK_SNAPSHOT:
    return forksnapshot(fs, &input);
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
