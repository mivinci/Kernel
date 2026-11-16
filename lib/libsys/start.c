#include "private/start.h"
static Bootview initial;
isize
sysstart(const Boot *boot, u64 available, const Bootview **out)
{
  Bootview view;
  isize    error;
  if (!out)
    return -ERR_INVALID;
  *out = NULL;
  error = bootcheck(boot, available, &view);
  if (error)
    return error;
  /* These records need fd/Fix adapters before the runtime may enter main. */
  if (boot->fd_count || boot->fix_bytes)
    return -ERR_NOTSUP;
  initial.header = view.header;
  initial.argv = view.argv;
  initial.env = view.env;
  initial.resources = view.resources;
  initial.fds = view.fds;
  *out = &initial;
  return 0;
}
const Bootview *
sysboot(void)
{
  return &initial;
}
