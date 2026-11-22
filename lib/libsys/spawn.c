#include "private/elf.h"

isize
sysspawnprepare(Elfwork *work, Handle create, const Createarg *args, Handle *control)
{
  Handle view, pid, management;
  u64    mask;
  isize  error;
  if (!work || !args || !control)
    return -ERR_INVALID;
  if (work->view || work->mapped || work->memory || work->reserved)
    return -ERR_STATE;
  error = __sys_sigmask(SIGNAL_BLOCK, 0, &mask);
  if (!error)
    error = __sys_create(create, args, &view, &pid, &management);
  work->failure = error;
  if (error)
    return error;
  work->view = view;
  work->pid = pid;
  work->image = 1;
  work->sigmask = mask;
  *control = management;
  return 0;
}

isize
sysspawnpublish(Elfwork *work, const Config *config)
{
  isize error;
  if (!work || !config)
    return -ERR_INVALID;
  if (!work->view || work->mapped || work->memory || work->failure || !config->entry)
    return -ERR_STATE;
  error = __sys_configure(work->view, config, 0);
  if (!error)
    error = __sys_start(work->view);
  if (error)
    return work->failure = error;
  work->view = 0;
  return work->pid;
}

isize
sysspawncancel(Elfwork *work)
{
  isize error;
  if (!work)
    return -ERR_INVALID;
  error = elfclean(work);
  if (error || !work->view)
    return error;
  error = __sys_destroy(work->view);
  if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
    return error;
  work->view = 0;
  return 0;
}
