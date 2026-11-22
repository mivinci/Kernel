#include "readdispatch.h"
#include <errno.h>

static isize
reject(Filelink *link, const Ipcmsg *call, isize status)
{
  Msg   reply = {{(u64) status, call->words[2], 0, 0}};
  isize error = ipc_reply(call->request, &reply);
  return error ? error : sysfilefinish(&link->watch.active, call->request);
}

static void
release(Fsreads *reads, Fsfiles *files, Readjob *job)
{
  reads->release(reads->backend, job);
  job->file->read = 0;
  fsfiledrop(files, job->file);
  *job = (Readjob){0};
}

static isize
advance(Fsreads *reads, Fsfiles *files, Readjob *job)
{
  Readfetch    fetch;
  isize        status;
  volatile u8 *page =
      (volatile u8 *) (job->link->address + FS_DATA_OFFSET + job->call.words[3] * FS_SLOT_BYTES);
  for (;;) {
    status =
        fileread(&job->work, &fetch, &job->link->watch.active, &job->call, &job->slot, job->map,
                 job->index, job->cache, job->first, &job->file->position, reads->scratch, page);
    if (status != 1)
      break;
    if (!reads->submit(reads->backend, job, &fetch))
      return 0;
    status = filereadcomplete(&job->work, &fetch.ticket, 0);
    if (status)
      return status;
  }
  if (!status)
    release(reads, files, job);
  return status == -ERR_WOULD_BLOCK ? 0 : status;
}

isize
fsreadstart(Fsreads *reads, Fsfiles *files, Filelink *link, const Ipcmsg *call,
            const Fileslot *slot)
{
  Fsfile  *file = fsfilefind(files, call->source, call->image, slot->object);
  Readjob *job;
  u32      i;
  isize    error;
  if (!file || !(file->access & VACCESS_R))
    return reject(link, call, file ? -EBADF : -ESTALE);
  if (!reads)
    return reject(link, call, -ENOSYS);
  for (i = 0; i < reads->count && reads->jobs[i].file; i++) {
  }
  if (file->read || i == reads->count || fsfilehold(file))
    return reject(link, call, -EAGAIN);
  job = &reads->jobs[i];
  job->file = file;
  job->link = link;
  job->call = *call;
  job->slot = *slot;
  file->read = call->request;
  error = reads->bind(reads->backend, job);
  if (error) {
    file->read = 0;
    fsfiledrop(files, file);
    *job = (Readjob){0};
    return reject(link, call, error);
  }
  if (ipc_defer(call->request)) {
    release(reads, files, job);
    return reject(link, call, -EAGAIN);
  }
  return advance(reads, files, job);
}

isize
fsreadpump(Fsreads *reads, Fsfiles *files)
{
  u32 i;
  if (reads)
    for (i = 0; i < reads->count; i++)
      if (reads->jobs[i].file) {
        isize error = advance(reads, files, &reads->jobs[i]);
        if (error)
          return error;
      }
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
