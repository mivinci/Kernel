#include <signal.h>
#include <string.h>

char *
strsignal(int signum)
{
  static char *const descriptions[SIGNAL_COUNT] = {
      [SIGHUP] = "Hangup",
      [SIGINT] = "Interrupt",
      [SIGQUIT] = "Quit",
      [SIGILL] = "Illegal instruction",
      [SIGABRT] = "Aborted",
      [SIGBUS] = "Bus error",
      [SIGFPE] = "Floating point exception",
      [SIGKILL] = "Killed",
      [SIGUSR1] = "User defined signal 1",
      [SIGSEGV] = "Segmentation fault",
      [SIGUSR2] = "User defined signal 2",
      [SIGPIPE] = "Broken pipe",
      [SIGALRM] = "Alarm clock",
      [SIGTERM] = "Terminated",
      [SIGCHLD] = "Child exited",
      [SIGCONT] = "Continued",
      [SIGSTOP] = "Stopped (signal)",
      [SIGTSTP] = "Stopped",
      [SIGTTIN] = "Stopped (terminal input)",
      [SIGTTOU] = "Stopped (terminal output)",
      [SIGWINCH] = "Window changed",
  };
  if (signum > 0 && signum < SIGNAL_COUNT && descriptions[signum])
    return descriptions[signum];
  return "Unknown signal";
}
