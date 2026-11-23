#include "private/libc.h"
#include "private/start.h"
#include <stdlib.h>
#include <unistd.h>
char **environ;
isize
libcenv(const Boot *boot, int *argc, char ***argv)
{
  char **envp;
  isize  error = sysstartargs(boot, argc, argv, &envp);
  if (!error)
    environ = envp;
  return error;
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
