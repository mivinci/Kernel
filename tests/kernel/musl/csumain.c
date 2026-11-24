#include <errno.h>
#include <signal.h>
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
  sigset_t mask, old, current;
  if (sigemptyset(&mask) || sigaddset(&mask, SIGUSR1) || sigprocmask(SIG_BLOCK, &mask, &old) ||
      sigismember(&old, SIGUSR1) || sigprocmask(SIG_SETMASK, 0, &current) ||
      sigismember(&current, SIGUSR1) != 1 || sigprocmask(SIG_SETMASK, &old, 0))
    return 44;
  if (initialized == 42 && tls == 42 && !errno && argc == 1 &&
      !strcmp(argv[0], "sbin/init") && envp)
    _exit(256 + 42);
  return 43;
}
