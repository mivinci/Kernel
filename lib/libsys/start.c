#include "private/start.h"
static Bootview initial;
static char    *arguments[BOOT_ARG_COUNT + 1];
static char    *environment[BOOT_ARG_COUNT + 1];
static char     strings[BOOT_ARG_BYTES];
isize
sysstartwithfds(const Boot *boot, u64 available, Bootfdinit install, void *context,
                const Bootview **out)
{
  Bootview view;
  isize    error;
  if (!out)
    return -ERR_INVALID;
  *out = NULL;
  error = bootcheck(boot, available, &view);
  if (error)
    return error;
  if (boot->fix_bytes || (boot->fd_count && !install))
    return -ERR_NOTSUP;
  if (boot->fd_count) {
    error = install(&view, context);
    if (error)
      return error;
  }
  initial.header = view.header;
  initial.argv = view.argv;
  initial.env = view.env;
  initial.resources = view.resources;
  initial.fds = view.fds;
  *out = &initial;
  return 0;
}

isize
sysstart(const Boot *boot, u64 available, const Bootview **out)
{
  return sysstartwithfds(boot, available, NULL, NULL, out);
}

isize
sysstartargswithfds(const Boot *boot, int *argc, char ***argv, char ***envp, Bootfdinit install)
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
  error = sysstartwithfds(boot, boot->bytes, install, NULL, &view);
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
isize
sysstartargs(const Boot *boot, int *argc, char ***argv, char ***envp)
{
  return sysstartargswithfds(boot, argc, argv, envp, NULL);
}
const Bootview *
sysboot(void)
{
  return &initial;
}
