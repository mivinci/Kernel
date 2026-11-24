#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static _Thread_local volatile int tls = 41;
static int initialized;
static volatile sig_atomic_t caught;

static void
handler(int sig)
{
  caught = sig;
}

extern long __sys_sigsend(const void *, unsigned long);

__attribute__((constructor)) static void
init(void)
{
  initialized = ++tls;
}

int
main(int argc, char **argv, char **envp)
{
  sigset_t mask, old, current;
  struct sigaction action = {0}, queried;
  const unsigned long self[] = {1, 1};
  int ends[2] = {-1, -1};
  if (sigemptyset(&mask) || sigaddset(&mask, SIGUSR1) || sigprocmask(SIG_BLOCK, &mask, &old) ||
      sigismember(&old, SIGUSR1) || sigprocmask(SIG_SETMASK, 0, &current) ||
      sigismember(&current, SIGUSR1) != 1 || sigprocmask(SIG_SETMASK, &old, 0))
    return 44;
  action.sa_handler = handler;
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGUSR1, &action, 0) || sigaction(SIGUSR1, 0, &queried) ||
      queried.sa_handler != handler || queried.sa_flags != SA_RESTART ||
      __sys_sigsend(self, SIGUSR1) || caught != SIGUSR1)
    return 45;
  if (initialized != 42 || tls != 42 || errno || argc != 1 ||
      strcmp(argv[0], "sbin/init") || !envp)
    return 43;
  char byte = 'x';
  if (read(0, &byte, 1) != -1 || errno != EBADF || byte != 'x' ||
      write(1, &byte, 1) != -1 || errno != EBADF)
    return 46;
  if (dup2(0, 1) != -1 || errno != EBADF || pipe(ends) != -1 || errno != ENOSYS ||
      ends[0] != -1 || ends[1] != -1)
    return 47;
  if (close(1) != -1 || errno != EBADF)
    return 48;
  _exit(256 + 42);
}
