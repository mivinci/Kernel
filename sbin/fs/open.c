#include "open.h"
#include "file.h"
#include <errno.h>
#include <string.h>

static int
getcwdcall(const Ipcmsg *call, const Fileslot *slot)
{
  return call->words[0] == FS_PATH && slot->args[0] == FS_PATH_GETCWD;
}

static int
readlinkcall(const Ipcmsg *call, const Fileslot *slot)
{
  return call->words[0] == FS_PATH && slot->args[0] == FS_PATH_READLINK;
}

static isize
replypath(Handle request, u64 seq, Filelink *link, isize status, u32 committed)
{
  Msg   reply = {{(u64) status, seq, 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    error = ipc_rqfinish(request);
  else {
    if (!committed && state & RQ_CHECK_CANCEL)
      reply.words[0] = (u64) -EINTR;
    error = ipc_reply(request, &reply);
  }
  return error || !link ? error : sysfilefinish(&link->watch.active, request);
}

static isize
finish(Handle request, u64 seq, Filelink *link, isize status)
{
  return replypath(request, seq, link, status, 0);
}

static isize
advancecwd(Fsopen *open, Openjob *job)
{
  Handle request = job->call.request;
  u64    state;
  isize  error;
  u32    steps;
  Msg    reply = {{0, job->slot.seq, 0, 0}};
  for (steps = 0; steps < 32; steps++) {
    error = ipc_rqcheck(request, &state);
    if (error)
      return error;
    if (state != RQ_CHECK_LIVE) {
      if (job->cwd.waiting) {
        error = open->cancel(job);
        if (error == -ERR_WOULD_BLOCK)
          return 0;
        if (error)
          return error;
        job->cwd.waiting = 0;
      }
      fscwddrop(&job->cwd);
      error = finish(request, job->slot.seq, job->link, -EINTR);
      break;
    }
    if (job->cwdready) {
      reply.words[0] = (u64) job->cwdstatus;
      reply.words[3] = job->cwdbytes;
      if (job->cwdbytes)
        memcpy((void *) (job->link->address + FS_DATA_OFFSET + job->call.words[3] * FS_SLOT_BYTES),
               job->cwd.path, job->cwdbytes);
      error = ipc_reply(request, &reply);
      if (!error)
        error = sysfilefinish(&job->link->watch.active, request);
      break;
    }
    job->cwdstatus = fscwdstep(&job->cwd, job->cwd.path, job->slot.args[1], &job->cwdbytes);
    if (job->cwdstatus == VWALK_WAIT)
      return 0;
    job->cwdready = job->cwdstatus != VWALK_MORE;
  }
  if (steps == 32) {
    open->again = 1;
    return 0;
  }
  if (!error)
    job->call.request = 0;
  return error;
}

static isize
advancelink(Fsopen *open, Openjob *job)
{
  Handle      request = job->call.request;
  const char *target;
  u64         targetbytes, state;
  isize       status, error;
  for (;;) {
    error = ipc_rqcheck(request, &state);
    if (error)
      return error;
    if (state != RQ_CHECK_LIVE) {
      if (job->waiting) {
        error = open->cancel(job);
        if (error == -ERR_WOULD_BLOCK)
          return 0;
        if (error)
          return error;
        job->waiting = 0;
      }
      vwalkdrop(&job->walk);
      status = -EINTR;
      break;
    }
    if (!job->node) {
      status = vwalkstep(&job->walk, &job->node);
      job->waiting = status == VWALK_WAIT;
      if (job->waiting)
        return 0;
      if (status == VWALK_MORE || job->node)
        continue;
      break;
    }
    if (job->node->kind != VNODE_LINK) {
      status = -EINVAL;
      break;
    }
    target = NULL;
    targetbytes = 0;
    status = open->ops->readlink(job, job->node, &target, &targetbytes);
    job->waiting = status == VWALK_WAIT;
    if (job->waiting)
      return 0;
    if (status == VWALK_MORE)
      continue;
    if (status)
      break;
    if (!target || targetbytes < 2 || targetbytes > FS_SLOT_BYTES || target[targetbytes - 1] ||
        memchr(target, 0, targetbytes - 1)) {
      status = -EIO;
      break;
    }
    error = ipc_rqcheck(request, &state);
    if (error)
      return error;
    if (state == RQ_CHECK_LIVE) {
      u64 bytes = MIN(targetbytes - 1, job->slot.args[3]);
      Msg reply = {{0, job->slot.seq, 0, bytes}};
      memcpy((void *) (job->link->address + FS_DATA_OFFSET + job->call.words[3] * FS_SLOT_BYTES),
             target, bytes);
      error = ipc_reply(request, &reply);
      if (!error)
        error = sysfilefinish(&job->link->watch.active, request);
      goto done;
    }
    status = -EINTR;
    break;
  }
  error = finish(request, job->slot.seq, job->link, status);
done:
  if (job->node) {
    open->ops->drop(job, job->node);
    job->node = NULL;
  }
  if (!error)
    job->call.request = 0;
  return error;
}

static isize
advance(Fsopen *open, Openjob *job)
{
  Handle request = job->call.request;
  Vnode *node = NULL;
  u64    state;
  isize  status, error;
  if (getcwdcall(&job->call, &job->slot))
    return advancecwd(open, job);
  if (readlinkcall(&job->call, &job->slot))
    return advancelink(open, job);
  if (job->changedcwd) {
    error = replypath(request, job->slot.seq, job->link, 0, 1);
    goto done;
  }
  if (job->node)
    goto complete;
  if (job->parent)
    goto create;
  for (;;) {
    error = ipc_rqcheck(request, &state);
    if (error)
      return error;
    if (state != RQ_CHECK_LIVE) {
      if (job->waiting) {
        error = open->cancel(job);
        if (error == -ERR_WOULD_BLOCK)
          return 0;
        if (error)
          return error;
        job->waiting = 0;
      }
      vwalkdrop(&job->walk);
      status = -EINTR;
      break;
    }
    status = vwalkstep(&job->walk, &node);
    job->waiting = status == VWALK_WAIT;
    if (job->waiting)
      return 0;
    if (status != VWALK_MORE)
      break;
  }
  if (node) {
    /* A backend step may have crossed a syscall boundary. Recheck before
       entering the backend's publication operation. */
    error = ipc_rqcheck(request, &state);
    if (!error && state == RQ_CHECK_LIVE) {
      if (job->call.words[0] == FS_PATH) {
        status = fscontextchdir(open->contexts, job->call.source, job->call.image, node);
        job->changedcwd = !status;
        error = replypath(request, job->slot.seq, job->link, status, job->changedcwd);
      } else if (status == VWALK_MISSING) {
        if (open->create) {
          job->parent = node;
          goto create;
        }
        error = finish(request, job->slot.seq, job->link, -ENOSYS);
      } else {
        job->node = node;
        goto complete;
      }
    } else if (!error)
      error = finish(request, job->slot.seq, job->link, -EINTR);
    open->ops->drop(job, node);
  } else
    error = finish(request, job->slot.seq, job->link, status);
  goto done;
complete:
  error = open->complete(job, job->node);
  if (error == OPEN_WAIT)
    return 0;
  open->ops->drop(job, job->node);
  job->node = NULL;
  goto completed;
create:
  error = ipc_rqcheck(request, &state);
  if (error)
    return error;
  error = open->create(job, job->parent, state != RQ_CHECK_LIVE);
  if (error == OPEN_WAIT)
    return 0;
  open->ops->drop(job, job->parent);
  job->parent = NULL;
completed:
  if (!error)
    error = sysfilefinish(&job->link->watch.active, request);
  else
    error = finish(request, job->slot.seq, job->link, -EIO);
done:
  if (!error)
    job->call.request = 0;
  return error;
}

isize
fsopenstart(Fsopen *open, Filelink *links, u32 count, const Ipcmsg *call)
{
  Handle           request = call->request;
  const Fscontext *context;
  Openjob         *job;
  Filelink        *link;
  Fileslot         slot;
  Vnode           *start;
  u32              follow;
  u32              i;
  isize            error;
  if (!open)
    return finish(request, call->words[2], NULL, -ENOSYS);
  error = sysfilelinkaccept(links, count, call, &slot);
  if (error)
    return finish(request, call->words[2], NULL,
                  error == -ERR_BUSY      ? -EBUSY
                  : error == -ERR_INVALID ? -EINVAL
                                          : -ESTALE);
  link = &links[call->words[1] & 65535];
  context = fscontextfind(open->contexts, call->source, call->image);
  if (!context)
    return finish(request, slot.seq, link, -ESTALE);
  follow = !readlinkcall(call, &slot);
  for (i = 0; i < open->count && open->jobs[i].call.request; i++) {
  }
  if (i == open->count)
    return finish(request, slot.seq, link, -EAGAIN);
  job = &open->jobs[i];
  job->call = *call;
  job->slot = slot;
  job->link = link;
  job->backend = open->backend;
  job->mask = context->mask;
  job->changedcwd = 0;
  job->waiting = 0;
  job->reserved = NULL;
  job->node = job->parent = NULL;
  if (getcwdcall(call, &slot)) {
    if (!open->cwdops || !open->cancel) {
      error = -ENOSYS;
      goto rejected;
    }
    if (slot.object || slot.span != 1 || slot.payload_bytes || !slot.args[1] ||
        slot.args[1] > FS_SLOT_BYTES) {
      error = -EINVAL;
      goto rejected;
    }
    for (i = 2; i < ARRAY_SIZE(slot.args); i++)
      if (slot.args[i]) {
        error = -EINVAL;
        goto rejected;
      }
    job->cwdready = job->cwdbytes = 0;
    error = fscwdinit(&job->cwd, open->mounts, open->cwdops, job, context);
    if (!error) {
      error = ipc_defer(request);
      if (!error)
        return advancecwd(open, job);
      fscwddrop(&job->cwd);
      error = -EAGAIN;
    }
    goto rejected;
  }
  start = context->cwd;
  error = 0;
  if (readlinkcall(call, &slot) && !open->cancel) {
    error = -ENOSYS;
    goto rejected;
  }
  if (call->words[0] == FS_OPEN) {
    error = fsfileoptions(&slot);
    if (!error)
      error = open->select(job, context, &start, &follow);
  }
  if (!error)
    error = vrequestpath(&job->walk, open->mounts, open->ops, job, context->root, start, follow,
                         &job->cred, link, call, &slot);
  if (!error) {
    if (call->words[0] == FS_OPEN && (slot.args[0] & FS_OPEN_CREAT))
      job->walk.create = slot.args[0] & FS_OPEN_EXCL ? VWALK_EXCLUSIVE : VWALK_CREATE;
    /* Reserve native deferred ownership before any backend can return WAIT. */
    error = ipc_defer(request);
    if (!error)
      return advance(open, job);
    vwalkdrop(&job->walk);
    error = -EAGAIN;
  }
rejected:
  error = finish(request, slot.seq, link, error);
  if (!error)
    job->call.request = 0;
  return error;
}

isize
fsopenpump(Fsopen *open)
{
  u32 i;
  if (!open)
    return 0;
  open->again = 0;
  for (i = 0; i < open->count; i++)
    if (open->jobs[i].call.request) {
      isize error = advance(open, &open->jobs[i]);
      if (error)
        return error;
    }
  return 0;
}

int
fsopenbusy(const Fsopen *open, const Filelink *link)
{
  u32 i;
  if (open)
    for (i = 0; i < open->count; i++)
      if (open->jobs[i].call.request && open->jobs[i].link == link)
        return 1;
  return 0;
}
