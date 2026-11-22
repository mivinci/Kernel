#include "readdispatch.h"
#include <errno.h>

static isize
reject(Filelink *link, const Ipcmsg *call, isize status)
{
  Msg   reply = {{(u64) status, call->words[2], 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  error =
      state == RQ_CHECK_DRAINING ? ipc_rqfinish(call->request) : ipc_reply(call->request, &reply);
  return error ? error : sysfilefinish(&link->watch.active, call->request);
}

static void
release(Fsreads *reads, Fsfiles *files, Readjob *job)
{
  if (job->map)
    reads->release(reads->backend, job);
  if (job->prev)
    job->prev->next = job->next;
  else
    job->file->head = job->next;
  if (job->next)
    job->next->prev = job->prev;
  else
    job->file->tail = job->prev;
  fsfiledrop(files, job->file);
  *job = (Readjob){0};
}

static isize
seek(Fsfiles *files, Fsfile *file, Filelink *link, const Ipcmsg *call, const Fileslot *slot)
{
  u64   length = 0, state;
  isize error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_LIVE && slot->args[1] == FS_SEEK_END) {
    error = files->length ? files->length(files->backend, file->node, &length) : -ENOSYS;
    if (error)
      return reject(link, call, error);
  }
  return fileseek(&link->watch.active, call, slot, length, &file->position);
}

static isize
grow(Fsreads *reads, Readjob *job, u64 state, u64 offset, const volatile u8 *input)
{
  Msg   reply = {{0, job->slot.seq, 0, 0}};
  isize error;
  if (!job->growdone) {
    u64   start = offset;
    isize result =
        reads->grow(reads->backend, job, state, offset, job->growing ? NULL : input, &start);
    job->growing = 1;
    if (!result)
      return -ERR_WOULD_BLOCK;
    if (result > 0) {
      if ((offset != FS_GROW_APPEND && start != offset) || start >= FS_FILE_BYTES ||
          (u64) result > job->slot.args[1] || (u64) result > FS_FILE_BYTES - start)
        return -ERR_STATE;
      if (!(job->slot.args[3] & FS_IO_POSITIONAL))
        job->file->position = start + (u64) result;
    }
    job->growresult = result;
    job->growdone = 1;
  }
  if (job->growresult < 0)
    reply.words[0] = (u64) job->growresult;
  else
    reply.words[2] = (u64) job->growresult;
  error = state == RQ_CHECK_DRAINING ? ipc_rqfinish(job->call.request)
                                     : ipc_reply(job->call.request, &reply);
  return error ? error : sysfilefinish(&job->link->watch.active, job->call.request);
}

static isize
advance(Fsreads *reads, Fsfiles *files, Readjob *job)
{
  Readfetch    fetch;
  isize        status;
  volatile u8 *page =
      (volatile u8 *) (job->link->address + FS_DATA_OFFSET + job->call.words[3] * FS_SLOT_BYTES);
  if (job->call.words[0] == FS_SEEK) {
    status = seek(files, job->file, job->link, &job->call, &job->slot);
    goto done;
  }
  if (!job->map) {
    u64 state;
    status = ipc_rqcheck(job->call.request, &state);
    if (status)
      return status;
    status = state == RQ_CHECK_LIVE ? reads->bind(reads->backend, job) : -EINTR;
    if (status) {
      status = reject(job->link, &job->call, status);
      goto done;
    }
  }
  if (reads->grow && job->loads && job->call.words[0] == FS_WRITE && job->slot.args[1] &&
      !job->work.request) {
    Writeplan plan;
    u64       state,
        offset = job->slot.args[3] & FS_IO_POSITIONAL ? job->slot.args[0] : job->file->position;
    int append = (job->file->flags & FS_OPEN_APPEND) && !(job->slot.args[3] & FS_IO_POSITIONAL);
    status = ipc_rqcheck(job->call.request, &state);
    if (status)
      return status;
    if (job->growing ||
        (state == RQ_CHECK_LIVE && (job->slot.args[3] & FS_IO_POSITIONAL || !job->slot.args[0]) &&
         (append || (!loadmapwrite(job->map, job->index, job->first, (u32) job->cache->blocks,
                                   offset, job->slot.args[1], &plan) &&
                     (!plan.block || plan.length != job->map->file.length))))) {
      status = grow(reads, job, state, append ? FS_GROW_APPEND : offset, page + job->slot.args[2]);
      goto done;
    }
  }
  for (;;) {
    status = filedata(&job->work, &fetch, &job->link->watch.active, &job->call, &job->slot,
                      job->map, job->index, job->cache, job->first, &job->file->position,
                      reads->scratch, page, job->loads);
    if (status != 1)
      break;
    if (!reads->submit(reads->backend, job, &fetch))
      return 0;
    status = filereadcomplete(&job->work, &fetch.ticket, NULL, 0);
    if (status)
      return status;
  }
done:
  if (!status)
    release(reads, files, job);
  return status == -ERR_WOULD_BLOCK ? 0 : status;
}

static isize
enqueue(Fsreads *reads, Fsfiles *files, Fsfile *file, Filelink *link, const Ipcmsg *call,
        const Fileslot *slot)
{
  Readjob *job;
  u32      i;
  for (i = 0; i < reads->count && reads->jobs[i].file; i++) {
  }
  if (i == reads->count || fsfilehold(file))
    return reject(link, call, -EAGAIN);
  if (ipc_defer(call->request)) {
    fsfiledrop(files, file);
    return reject(link, call, -EAGAIN);
  }
  job = &reads->jobs[i];
  job->file = file;
  job->link = link;
  job->call = *call;
  job->slot = *slot;
  job->prev = file->tail;
  if (file->tail)
    file->tail->next = job;
  else
    file->head = job;
  file->tail = job;
  return file->head == job ? advance(reads, files, job) : 0;
}

isize
fsdatastart(Fsreads *reads, Fsfiles *files, Filelink *link, const Ipcmsg *call,
            const Fileslot *slot)
{
  Fsfile *file = fsfilefind(files, call->source, call->image, slot->object);
  u32     access = call->words[0] == FS_WRITE ? VACCESS_W : VACCESS_R;
  if (!file || !(file->access & access))
    return reject(link, call, file ? -EBADF : -ESTALE);
  if (access == VACCESS_W) {
    u64 flags = slot->args[3];
    if ((flags & FS_IO_APPEND) && (!(file->flags & FS_OPEN_APPEND) || (flags & FS_IO_POSITIONAL)))
      return reject(link, call, -EINVAL);
    if ((file->flags & FS_OPEN_APPEND) && !(flags & FS_IO_POSITIONAL) && (!reads || !reads->grow))
      return reject(link, call, -ENOSYS);
  }
  return reads ? enqueue(reads, files, file, link, call, slot) : reject(link, call, -ENOSYS);
}

isize
fsseekstart(Fsreads *reads, Fsfiles *files, Fsfile *file, Filelink *link, const Ipcmsg *call,
            const Fileslot *slot)
{
  return file->head ? enqueue(reads, files, file, link, call, slot)
                    : seek(files, file, link, call, slot);
}

isize
fsreadpump(Fsreads *reads, Fsfiles *files)
{
  u32 i, progress;
  if (!reads)
    return 0;
  do {
    progress = 0;
    for (i = 0; i < reads->count; i++)
      if (reads->jobs[i].file) {
        Readjob *job = &reads->jobs[i];
        isize    error;
        if (job->prev) {
          u64 state;
          error = ipc_rqcheck(job->call.request, &state);
          if (error)
            return error;
          if (state == RQ_CHECK_LIVE)
            continue;
          /* Only the head may own a backend binding or a read ticket. */
          error = reject(job->link, &job->call, -EINTR);
          if (!error)
            release(reads, files, job);
        } else
          error = advance(reads, files, job);
        if (error)
          return error;
        progress |= !job->file;
      }
  } while (progress);
  return 0;
}

int
fsreadbusy(const Fsreads *reads, const Filelink *link)
{
  u32 i;
  if (reads)
    for (i = 0; i < reads->count; i++)
      if (reads->jobs[i].file && reads->jobs[i].link == link)
        return 1;
  return 0;
}

isize
fsreadcomplete(Readjob *job, Handle request, const Cacheio *ticket, const void *block, u32 bytes)
{
  if (!job || !job->file || job->call.request != request)
    return -ERR_STALE;
  return filereadcomplete(&job->work, ticket, block, bytes);
}
