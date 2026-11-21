#include "private/elf.h"

isize
sysexeccancel(Elfwork *work)
{
  isize cleanup, error;
  if (!work)
    return -ERR_INVALID;
  cleanup = elfclean(work);
  if (!work->view)
    return cleanup;
  error = __sys_execabort(work->view);
  if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
    return error;
  work->view = 0;
  return cleanup;
}

isize
sysexecimage(const Elfsource *source, const Elfplan *plan, const void *boot, u64 bytes, u64 reserve,
             Elfwork *work)
{
  Config config;
  Handle parent;
  u64    image;
  isize  error, cleanup;
  if (!work || work->view || work->mapped || work->memory || work->reserved || work->sigmask)
    return -ERR_INVALID;
  work->failure = 0;
  error = __sys_getid(&work->pid, &image, &parent);
  if (!error)
    error = __sys_execprep(work->parent_space, reserve, &work->view, &work->image);
  if (error)
    return work->failure = error;
  error = elfload(source, plan, boot, bytes, work, &config);
  if (!error) {
    error = __sys_execcommit(work->view, &config);
    if (!error)
      error = -ERR_STATE;
  }
  if (!work->failure)
    work->failure = error;
  cleanup = sysexeccancel(work);
  return cleanup ? cleanup : work->failure;
}
