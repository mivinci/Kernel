#include "private/elf.h"

isize
sysforkcancel(Elfwork *work)
{
  isize cleanup, error;
  if (!work)
    return -ERR_INVALID;
  cleanup = elfclean(work);
  if (!work->view)
    return cleanup;
  /* Revoke the child's publication even if parent-local cleanup must retry. */
  error = __sys_forkabort(work->view);
  if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
    return error;
  work->view = 0;
  return cleanup;
}

isize
sysforkpublish(Elfwork *work, u64 address, const void *data, u64 bytes)
{
  Handle pid;
  isize  error, cancel;
  if (!work || work->view <= 0 || work->pid <= 0 || work->mapped || work->memory || work->reserved)
    return -ERR_INVALID;
  if (work->failure)
    return -ERR_STATE;
  error = sysfixload(work, address, data, bytes);
  if (!error) {
    error = __sys_forkcommit(work->view, (const void *) address, bytes, &pid);
    if (!error) {
      work->view = 0;
      if (pid != work->pid)
        __builtin_trap();
      return pid;
    }
  }
  if (!work->failure)
    work->failure = error;
  cancel = sysforkcancel(work);
  return cancel ? cancel : work->failure;
}
