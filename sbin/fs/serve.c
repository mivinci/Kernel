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
forkrelease(Fsservice *fs, Fsfork *record, u32 count)
{
  while (count) {
    u32       i = --count;
    u64       session = record->manifest.groups[i].session;
    Forkhold *hold = &record->holds[i];
    if ((session & FS_FILE_SESSION) ? !hold->file : !hold->pipe)
      continue;
    if (session & FS_FILE_SESSION) {
      fsfiledrop(fs->pipes.regular, hold->file);
      hold->file = NULL;
    } else {
      pipedrop(&hold->pipe->stream,
               hold->pipe->session[PIPE_WRITE] == session ? PIPE_WRITE : PIPE_READ);
      hold->pipe = NULL;
    }
  }
  record->manifest.count = 0;
  record->context = 0;
}

isize
fsforktransfer(Fsservice *fs, Fsfork *record, Connpeer child)
{
  u32 count, i;
  isize error;
  if (!fs || !record || !pidvalid(child.pid) || !child.image ||
      child.pid != record->manifest.child || child.image != record->manifest.target_image)
    return -ERR_INVALID;
  count = record->manifest.count;
  if (!count || count > BOOT_FD_COUNT)
    return -ERR_STATE;
  for (i = 0; i < count; i++) {
    const Execgroup *group = &record->manifest.groups[i];
    Forkhold *hold = &record->holds[i];
    if (group->session & FS_FILE_SESSION)
      error = fsfileinherit(fs->pipes.regular, child.pid, child.image, group->session,
                            group->reference, &hold->file);
    else
      error = pipefileinherit(fs->pipes.table, child.pid, child.image,
                              (Pipekey){group->session, group->reference}, &hold->pipe);
    if (error) {
      while (i) {
        const Execgroup *moved = &record->manifest.groups[--i];
        if (moved->session & FS_FILE_SESSION)
          (void) fsfileclose(fs->pipes.regular, child.pid, child.image, moved->session,
                             moved->reference);
        else
          (void) pipefileclose(fs->pipes.table, child.pid, child.image,
                               (Pipekey){moved->session, moved->reference});
      }
      forkrelease(fs, record, count);
      return error;
    }
  }
  record->manifest.count = 0;
  record->context = 0;
  return 0;
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
        forkrelease(fs, record, m->count);
    } else if (m->deadline <= now)
      forkrelease(fs, record, m->count);
    else if (!*deadline || m->deadline < *deadline)
      *deadline = m->deadline;
  }
  return 0;
}

static isize
forkref(void *service, Handle pid, u64 image, u64 session, u64 reference)
{
  Fsservice *fs = service;
  if (session & FS_FILE_SESSION)
    return fsfilerefcheck(fs->pipes.regular, pid, image, session, reference);
  return pipefileref(fs->pipes.table, pid, image, (Pipekey){session, reference}, NULL);
}

static isize
forkhold(Fsservice *fs, Fsfork *record)
{
  Execmanifest *m = &record->manifest;
  u32           i;
  isize         error;
  for (i = 0; i < m->count; i++) {
    const Execgroup *g = &m->groups[i];
    if (g->session & FS_FILE_SESSION)
      error = fsfileholdref(fs->pipes.regular, m->pid, m->source_image, g->session, g->reference,
                            &record->holds[i].file);
    else
      error = pipefileref(fs->pipes.table, m->pid, m->source_image,
                          (Pipekey){g->session, g->reference}, &record->holds[i].pipe);
    if (error) {
      forkrelease(fs, record, i);
      return error;
    }
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
  for (i = 0; i < fs->forks->count &&
              (fs->forks->records[i].manifest.count || fs->forks->records[i].token); i++) {
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
      forkref, fs, fs->forks->serial + 1);
  if (!error) {
    error = forkhold(fs, record);
    if (!error) {
      fs->forks->serial++;
      record->context = context->version;
      *captured = record;
      return 0;
    }
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
  if (fs->forks && fs->open) {
    error = sysfilelinkaccept(fs->links, fs->nlinks, call, &slot);
    if (!error)
      link = &fs->links[call->words[1] & 65535];
    else
      reply.words[0] = (u64) (error == -ERR_INVALID ? -EINVAL
                              : error == -ERR_BUSY  ? -EBUSY
                                                    : -ESTALE);
  }
  error = ipc_rqcheck(call->request, &state);
  if (!error && link && state == RQ_CHECK_LIVE) {
    reply.words[0] = (u64) forkcapture(fs, call, link, &slot, &record);
    /* Shared-slot capture can be preempted by cancellation or image exit. */
    error = ipc_rqcheck(call->request, &state);
  }
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
    forkrelease(fs, record, record->manifest.count);
  return error || !link ? error : sysfilefinish(&link->watch.active, call->request);
}

static isize
forkprepare(Fsservice *fs, const Ipcmsg *call)
{
  static Execmanifest plan;
  Fileslot slot;
  Fsfork *record = NULL;
  Filelink *source, *target = NULL;
  Connpeer self, child;
  Handle parent;
  u64 now, state, token;
  u32 i;
  isize error, status = -ESTALE;
  int context = 0, transferred = 0;
  if (!fs->forks || !fs->open || !fs->open->contexts)
    return reject(call, -ENOSYS);
  error = sysfilelinkaccept(fs->links, fs->nlinks, call, &slot);
  if (error)
    return reject(call, -ESTALE);
  source = &fs->links[call->words[1] & 65535];
  token = slot.args[6];
  error = ipc_rqcheck(call->request, &state);
  if (error || state != RQ_CHECK_LIVE)
    goto reply;
  for (i = 0; token && i < fs->forks->count; i++)
    if (fs->forks->records[i].manifest.count &&
        fs->forks->records[i].manifest.snapshot == token) {
      record = &fs->forks->records[i];
      break;
    }
  if (!record)
    goto reply;
  if (__sys_getid(&self.pid, &self.image, &parent) || sysfilenow(&now)) {
    status = -EIO;
    goto reply;
  }
  plan.count = 0;
  error = sysforkprepare(&plan, &record->manifest, &source->watch, call, &slot,
                         (const void *) (source->address + FS_DATA_OFFSET +
                                         call->words[3] * FS_SLOT_BYTES), self, now);
  if (error) {
    status = error == -ERR_TIMEDOUT ? -ETIMEDOUT : -ESTALE;
    goto recheck;
  }
  child.pid = plan.child;
  child.image = plan.target_image;
  for (i = 0; i < fs->nlinks; i++) {
    Filelink *link = &fs->links[i];
    if (!link->watch.owner.cookie && !link->watch.connid && !link->memory && !link->address &&
        !link->ready && !link->prepaid)
      break;
  }
  status = -ENFILE;
  if (i == fs->nlinks)
    goto recheck;
  target = &fs->links[i];
  status = -ESTALE;
  if (sysforkobserve(&target->watch.owner, fs->observe, call, child.pid, child.image))
    goto recheck;
  status = fscontextinherit(fs->open->contexts, plan.pid, plan.source_image, record->context,
                            child.pid, child.image);
  if (status)
    goto recheck;
  context = 1;
  status = -ENFILE;
  if (sysfileprepay(fs->links, fs->nlinks, i, fs->allocator, fs->space, fs->arena))
    goto recheck;
  status = -EIO;
  if (sysfilenow(&now))
    goto recheck;
  if (now >= plan.deadline || now >= record->manifest.deadline) {
    status = -ETIMEDOUT;
    goto recheck;
  }
  status = -ESTALE;
  if (sysforktarget(&target->watch.owner) != SYSEXEC_PENDING)
    goto recheck;
  status = -ENFILE;
  if (fsforktransfer(fs, record, child))
    goto recheck;
  transferred = 1;
  record->link = i;
  record->token = token;
  status = 0;
recheck:
  error = ipc_rqcheck(call->request, &state);
reply:
  if (!error) {
    if (state == RQ_CHECK_DRAINING)
      error = ipc_rqfinish(call->request);
    else
      error = reject(call, state & RQ_CHECK_CANCEL ? -EINTR : status);
  }
  if (target && (error || status || state != RQ_CHECK_LIVE)) {
    if (transferred) {
      record->token = 0;
      if (fs->pipes.regular)
        fsfileretire(fs->pipes.regular, child.pid, child.image);
      if (fs->pipes.table)
        (void) pipefileretire(fs->pipes.table, child.pid, child.image);
    }
    if (context)
      (void) fscontextremove(fs->open->contexts, child.pid, child.image);
    (void) sysfilelinkclose(target);
  }
  return error ? error : sysfilefinish(&source->watch.active, call->request);
}

static isize
forkattach(Fsservice *fs, const Ipcmsg *call)
{
  u32 i;
  u64 state;
  isize status = ipc_rqcheck(call->request, &state);
  if (status)
    return status;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (state & RQ_CHECK_CANCEL)
    return reject(call, -EINTR);
  if (call->words[1] || !call->words[2] || !call->words[3])
    return reject(call, -EINVAL);
  for (i = 0; fs->forks && i < fs->forks->count; i++) {
    Fsfork *record = &fs->forks->records[i];
    Filelink *link;
    isize error;
    if (record->token != call->words[3] || record->link >= fs->nlinks)
      continue;
    link = &fs->links[record->link];
    if (!link->prepaid || link->watch.owner.pid != call->source ||
        link->watch.owner.image != call->image ||
        sysforktarget(&link->watch.owner) != SYSEXEC_ACTIVE)
      return reject(call, -ESTALE);
    Resreply reply = {link->memory, MAP_READ | MAP_WRITE, 0, FS_CONN_BYTES,
                      {call->words[2], link->watch.connid, FS_CONN_BYTES}};
    error = ipc_reply_resource(call->request, &reply);
    if (!error) {
      link->prepaid = 0;
      link->ready = 1;
      record->token = 0;
    } else if (!ipc_rqcheck(call->request, &state) && state == RQ_CHECK_DRAINING) {
      return ipc_rqfinish(call->request);
    }
    return error;
  }
  return reject(call, -ESTALE);
}

static isize
drain(void *context, Filelink *link)
{
  Fsservice *fs = context;
  isize      error;
  u32        i;
  forksweep(fs, link, NULL);
  if (fs->forks)
    for (i = 0; i < fs->forks->count; i++)
      if (fs->forks->records[i].token && fs->forks->records[i].link < fs->nlinks &&
          &fs->links[fs->forks->records[i].link] == link)
        fs->forks->records[i].token = 0;
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
  deadline = fs->loads ? loaddeadline(fs->loads->table) : 0;
  error = forksweep(fs, NULL, &deadline);
  if (error)
    return error;
  /* Expiring a snapshot may unblock the opposite endpoint immediately. */
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
  case FS_FORK_PREPARE:
    return forkprepare(fs, &input);
  case FS_ATTACH:
    return forkattach(fs, &input);
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
