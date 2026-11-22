#include "loaddispatch.h"
#include "blk.h"
#include <errno.h>

static isize
finish(const Ipcmsg *call, Filelink *link, isize status)
{
  Msg   reply = {{(u64) status, call->words[2], 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    error = ipc_rqfinish(call->request);
  else {
    if (state & RQ_CHECK_CANCEL)
      reply.words[0] = (u64) -EINTR;
    error = ipc_reply(call->request, &reply);
  }
  return error || !link ? error : sysfilefinish(&link->watch.active, call->request);
}

static isize
loaderrno(isize error)
{
  switch (error) {
  case -ERR_INVALID:
    return -EINVAL;
  case -ERR_BUSY:
    return -EBUSY;
  case -ERR_STALE:
    return -ESTALE;
  case -ERR_DENIED:
    return -EACCES;
  case -ERR_TIMEDOUT:
    return -ETIMEDOUT;
  case -ERR_CANCELLED:
    return -EINTR;
  default:
    return -EIO;
  }
}

static isize
advance(Fsloads *loads, Loadjob *job, u64 now)
{
  Loadfetch fetch;
  isize     error;
  if (!job->work.request) {
    Loadspan span;
    Cache   *cache;
    u64      state;
    error = ipc_rqcheck(job->io.request, &state);
    if (error)
      return error;
    if (job->io.staged || state != RQ_CHECK_LIVE ||
        loadstatus(job->io.table, job->io.session, job->io.ticket, now))
      return loadioreply(&job->io, NULL, 0, now);
    error = loads->resolve(loads->backend, &job->io, &cache, &span);
    if (error == -ERR_WOULD_BLOCK)
      return 0;
    if (!error)
      error = loadcachebegin(&job->work, &job->io, cache, &span);
    if (error) {
      job->io.reply.words[0] = (u64) loaderrno(error);
      job->io.staged = 1;
      return loadioreply(&job->io, NULL, 0, now);
    }
  }
  error = loadcachepump(&job->work, &job->io, &fetch, loads->scratch, now);
  if (error == 1) {
    error = loads->submit(loads->backend, job, &fetch);
    if (!error)
      return 0;
    if (error == -ERR_WOULD_BLOCK)
      return loadcacheretry(&job->work, job->io.request, &fetch.ticket);
    error = loadcachecomplete(&job->work, job->io.request, &fetch.ticket, NULL, 0);
    if (error)
      return error;
    error = loadcachepump(&job->work, &job->io, &fetch, loads->scratch, now);
  }
  return error == -ERR_WOULD_BLOCK ? 0 : error;
}

isize
fsloadstart(Fsloads *loads, Filelink *links, u32 count, const Ipcmsg *call)
{
  Fileslot     slot;
  Filelink    *link;
  Loadjob     *job;
  volatile u8 *page;
  u32          i;
  u64          now;
  isize        error;
  if (!loads)
    return finish(call, NULL, -ENOSYS);
  error = sysfilelinkaccept(links, count, call, &slot);
  if (error)
    return finish(call, NULL, loaderrno(error));
  link = &links[call->words[1] & 65535];
  for (i = 0; i < loads->count && loads->jobs[i].io.request; i++) {
  }
  if (i == loads->count)
    return finish(call, link, -EAGAIN);
  error = sysfilenow(&now);
  if (error)
    return finish(call, link, -EIO);
  job = &loads->jobs[i];
  page = (volatile u8 *) (link->address + FS_DATA_OFFSET + call->words[3] * FS_SLOT_BYTES);
  error = loadiostart(&job->io, loads->table, &link->watch.active, call, &slot, page, now);
  if (error)
    return finish(call, link, loaderrno(error));
  job->link = link;
  error = ipc_defer(call->request);
  if (error)
    job->io.reply.words[0] = (u64) -EAGAIN;
  job->io.staged = error != 0;
  error = sysfilenow(&now);
  return error ? error : advance(loads, job, now);
}

isize
fsloadpump(Fsloads *loads)
{
  u64   now;
  u32   i;
  isize error;
  if (!loads)
    return 0;
  error = sysfilenow(&now);
  if (error)
    return error;
  loadsweep(loads->table, now);
  for (i = 0; i < loads->count; i++)
    if (loads->jobs[i].io.request) {
      error = advance(loads, &loads->jobs[i], now);
      if (error)
        return error;
    }
  return 0;
}

int
fsloadretire(Fsloads *loads, const Filelink *link)
{
  u32 i;
  if (!loads)
    return 0;
  loadretire(loads->table, link->watch.owner.pid, link->watch.owner.image);
  for (i = 0; i < loads->count; i++)
    if (loads->jobs[i].io.request && loads->jobs[i].link == link)
      return 1;
  return 0;
}

static isize
loadfetched(void *context, Handle request, const Cacheio *ticket, isize status, const void *data)
{
  Loadjob *job = context;
  return status == -EAGAIN
             ? loadcacheretry(&job->work, request, ticket)
             : loadcachecomplete(&job->work, request, ticket, data, data ? CACHE_BYTES : 0);
}

isize
blkfetchload(void *context, Loadjob *job, const Loadfetch *fetch)
{
  return blkfetchstart(context, job, job->io.request, &fetch->ticket, fetch->block, loadfetched);
}
