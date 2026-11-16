#include "private/libc.h"
#include <stdlib.h>
#include <unistd.h>
extern Exitfn __preinit_array_start[], __preinit_array_end[];
extern Exitfn __init_array_start[], __init_array_end[];
extern Exitfn __fini_array_start[], __fini_array_end[];
static void
finalize(void)
{
  Exitfn *entry = __fini_array_end;
  while (entry != __fini_array_start)
    (*--entry)();
}
NORETURN void
libcstart(const Boot *boot, int (*entry)(int, char **))
{
  Exitfn *function;
  char  **argv;
  int     argc;
  if (libcenv(boot, &argc, &argv))
    _exit(127);
  libcexitinit(finalize);
  for (function = __preinit_array_start; function != __preinit_array_end; function++)
    (*function)();
  for (function = __init_array_start; function != __init_array_end; function++)
    (*function)();
  exit(entry(argc, argv));
}
