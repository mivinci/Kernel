#include "private/start.h"
static Bootview initial;
static char    *arguments[BOOT_ARG_COUNT + 1];
static char    *environment[BOOT_ARG_COUNT + 1];
static char     strings[BOOT_ARG_BYTES];
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

isize
sysstartargs(const Boot *boot, int *argc, char ***argv, char ***envp)
{
  const Bootview *view;
  const Strref   *refs;
  const u8       *source = (const u8 *) boot;
  char          **pointers;
  u64             layer, i, j, count, offset = 0;
  isize           error;
  if (!argc || !argv || !envp)
    return -ERR_INVALID;
  *argc = 0;
  *argv = *envp = NULL;
  if (!boot)
    return -ERR_INVALID;
  error = sysstart(boot, boot->bytes, &view);
  if (error)
    return error;
  for (layer = 0; layer < 2; layer++) {
    refs = layer ? view->env : view->argv;
    pointers = layer ? environment : arguments;
    count = layer ? boot->envc : boot->argc;
    for (i = 0; i < count; i++) {
      pointers[i] = &strings[offset];
      for (j = 0; j < refs[i].length; j++)
        strings[offset++] = (char) source[refs[i].offset + j];
    }
    pointers[count] = NULL;
  }
  *argc = (int) boot->argc;
  *argv = arguments;
  *envp = environment;
  return 0;
}
const Bootview *
sysboot(void)
{
  return &initial;
}
