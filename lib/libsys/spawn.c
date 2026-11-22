#include "private/elf.h"

isize
sysspawnprepare(Spawnwork *work, Handle create, const Createarg *args, const Spawnsig *sig,
                Handle *control)
{
  Elfwork *elf;
  Handle   view, pid, management;
  u64      mask = 0, defaults = 0;
  isize    error = 0;
  if (!work || !args || !control)
    return -ERR_INVALID;
  elf = &work->elf;
  if (elf->view || elf->mapped || elf->memory || elf->reserved || work->config.entry)
    return -ERR_STATE;
  if (sig) {
    if (sig->setmask > 1 || (sig->defaults & ~SIGNAL_SUPPORTED) ||
        (sig->setmask && (sig->mask & ~SIGNAL_SUPPORTED)))
      return -ERR_INVALID;
    defaults = sig->defaults;
  }
  if (sig && sig->setmask)
    mask = sig->mask & ~SIGNAL_UNMASKABLE;
  else
    error = __sys_sigmask(SIGNAL_BLOCK, 0, &mask);
  if (!error)
    error = __sys_create(create, args, &view, &pid, &management);
  elf->failure = error;
  if (error)
    return error;
  elf->view = view;
  elf->pid = pid;
  elf->image = 1;
  elf->sigmask = mask;
  work->defaults = defaults;
  *control = management;
  return 0;
}

isize
sysspawnfill(Spawnwork *work, const Elfsource *source, const Elfplan *plan, const void *boot,
             u64 bytes)
{
  isize error;
  if (!work)
    return -ERR_INVALID;
  if (!work->elf.view || work->elf.failure || work->config.entry)
    return -ERR_STATE;
  error = elfload(source, plan, boot, bytes, &work->elf, &work->config);
  if (error && !work->elf.failure)
    work->elf.failure = error;
  if (!error)
    work->config.sigdefault = work->defaults;
  return error;
}

isize
sysspawnpublish(Spawnwork *work)
{
  Elfwork *elf;
  isize    error;
  if (!work)
    return -ERR_INVALID;
  elf = &work->elf;
  if (!elf->view || elf->mapped || elf->memory || elf->failure || !work->config.entry)
    return -ERR_STATE;
  error = __sys_configure(elf->view, &work->config, 0);
  if (!error)
    error = __sys_start(elf->view);
  if (error)
    return elf->failure = error;
  elf->view = 0;
  work->config.entry = 0;
  return elf->pid;
}

isize
sysspawncancel(Spawnwork *work)
{
  isize error;
  if (!work)
    return -ERR_INVALID;
  work->config.entry = 0;
  error = elfclean(&work->elf);
  if (error || !work->elf.view)
    return error;
  error = __sys_destroy(work->elf.view);
  if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
    return error;
  work->elf.view = 0;
  return 0;
}
