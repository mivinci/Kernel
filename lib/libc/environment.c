#include "private/libc.h"
#include "private/start.h"
#include <stdlib.h>
#include <unistd.h>
static char *arguments[BOOT_ARG_COUNT + 1];
static char *environment[BOOT_ARG_COUNT + 1];
static char  strings[BOOT_ARG_BYTES];
char       **environ;
isize
libcenv(const Boot *boot, int *argc, char ***argv)
{
  const Bootview *view;
  const Strref   *refs;
  const u8       *source = (const u8 *) boot;
  char          **pointers;
  u64             layer, i, j, count, offset = 0;
  isize           error;
  if (!argc || !argv)
    return -ERR_INVALID;
  *argc = 0;
  *argv = NULL;
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
  environ = environment;
  *argc = (int) boot->argc;
  *argv = arguments;
  return 0;
}
const Boot *
libcboot(void)
{
  return sysboot()->header;
}
char *
getenv(const char *name)
{
  u64         i, length = 0;
  const char *candidate;
  if (!name || !*name || !environ)
    return NULL;
  while (name[length]) {
    if (name[length] == '=')
      return NULL;
    length++;
  }
  for (i = 0; environ[i]; i++) {
    u64 j;
    candidate = environ[i];
    for (j = 0; j < length && candidate[j] == name[j]; j++) {
    }
    if (j == length && candidate[j] == '=')
      return environ[i] + length + 1;
  }
  return NULL;
}
