#include "open.h"
#include <errno.h>

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
advance(Fsopen *open, Openjob *job)
{
  Handle request = job->call.request;
  Vnode *node = NULL;
  u64    state;
  isize  status, error;
  if (job->changedcwd) {
    error = replypath(request, job->slot.seq, job->link, 0, 1);
    goto done;
  }
  if (job->node)
    goto complete;
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
  u32              follow = 1, i;
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
  start = context->cwd;
  error = call->words[0] == FS_OPEN ? open->select(job, context, &start, &follow) : 0;
  if (!error)
    error = vrequestpath(&job->walk, open->mounts, open->ops, job, context->root, start, follow,
                         &job->cred, link, call, &slot);
  if (!error) {
    /* Reserve native deferred ownership before any backend can return WAIT. */
    error = ipc_defer(request);
    if (!error)
      return advance(open, job);
    vwalkdrop(&job->walk);
    error = -EAGAIN;
  }
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
