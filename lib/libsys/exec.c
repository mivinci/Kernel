#include "private/elf.h"

isize
sysexecprepare(Execwork *work, u64 reserve)
{
  Elfwork *elf;
  Handle   parent;
  u64      image;
  isize    error;
  if (!work)
    return -ERR_INVALID;
  elf = &work->elf;
  if (elf->view || elf->mapped || elf->memory || elf->reserved || elf->sigmask ||
      work->config.entry)
    return -ERR_STATE;
  elf->failure = 0;
  error = __sys_getid(&elf->pid, &image, &parent);
  if (!error)
    error = __sys_execprep(elf->parent_space, reserve, &elf->view, &elf->image);
  return elf->failure = error;
}

isize
sysexecfill(Execwork *work, const Elfsource *source, const Elfplan *plan, const void *boot,
            u64 bytes)
{
  Elfwork *elf;
  isize    error;
  if (!work)
    return -ERR_INVALID;
  elf = &work->elf;
  if (!elf->view || elf->failure || work->config.entry)
    return -ERR_STATE;
  error = elfexec(source, plan, boot, bytes, elf, &work->config);
  if (error && !elf->failure)
    elf->failure = error;
  return error;
}

isize
sysexecpublish(Execwork *work)
{
  Elfwork *elf;
  isize    error;
  if (!work)
    return -ERR_INVALID;
  elf = &work->elf;
  if (!elf->view || elf->mapped || elf->memory || elf->failure || !work->config.entry)
    return -ERR_STATE;
  error = __sys_execcommit(elf->view, &work->config);
  return elf->failure = error ? error : -ERR_STATE;
}

isize
sysexeccancel(Execwork *work)
{
  Elfwork *elf;
  isize    cleanup, error;
  if (!work)
    return -ERR_INVALID;
  elf = &work->elf;
  work->config.entry = 0;
  cleanup = elfclean(elf);
  if (!elf->view)
    return cleanup;
  error = __sys_execabort(elf->view);
  if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
    return error;
  elf->view = 0;
  return cleanup;
}
