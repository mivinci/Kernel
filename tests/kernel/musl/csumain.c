#include <errno.h>
#include <string.h>
#include <unistd.h>

static _Thread_local volatile int tls = 41;
static int initialized;

__attribute__((constructor)) static void
init(void)
{
  initialized = ++tls;
}

int
main(int argc, char **argv, char **envp)
{
  if (initialized == 42 && tls == 42 && !errno && argc == 1 &&
      !strcmp(argv[0], "sbin/init") && envp)
    _exit(256 + 42);
  return 43;
}
