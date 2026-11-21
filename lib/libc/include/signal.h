#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H
#include <sys/types.h>
#include <abi/signal.h>
#define SIGHUP        SIGNAL_HUP
#define SIGINT        SIGNAL_INT
#define SIGQUIT       SIGNAL_QUIT
#define SIGILL        SIGNAL_ILL
#define SIGABRT       SIGNAL_ABRT
#define SIGBUS        SIGNAL_BUS
#define SIGFPE        SIGNAL_FPE
#define SIGKILL       SIGNAL_KILL
#define SIGUSR1       SIGNAL_USR1
#define SIGSEGV       SIGNAL_SEGV
#define SIGUSR2       SIGNAL_USR2
#define SIGPIPE       SIGNAL_PIPE
#define SIGALRM       SIGNAL_ALRM
#define SIGTERM       SIGNAL_TERM
#define SIGCHLD       SIGNAL_CHLD
#define SIGCONT       SIGNAL_CONT
#define SIGSTOP       SIGNAL_STOP
#define SIGTSTP       SIGNAL_TSTP
#define SIGTTIN       SIGNAL_TTIN
#define SIGTTOU       SIGNAL_TTOU
#define SIGWINCH      SIGNAL_WINCH
#define SIG_BLOCK     SIGNAL_BLOCK
#define SIG_UNBLOCK   SIGNAL_UNBLOCK
#define SIG_SETMASK   SIGNAL_SETMASK
#define SA_NODEFER    SIGNAL_NODEFER
#define SA_RESETHAND  SIGNAL_RESETHAND
#define SA_NOCLDSTOP  SIGNAL_NOCLDSTOP
#define SA_NOCLDWAIT  SIGNAL_NOCLDWAIT
#define SA_SIGINFO    SIGNAL_SIGINFO
#define SA_RESTART    SIGNAL_RESTART
#define SIG_DFL       ((void (*)(int)) 0)
#define SIG_IGN       ((void (*)(int)) 1)
#define SI_USER       0
#define SI_KERNEL     128
#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_STOPPED   5
#define CLD_CONTINUED 6
#define SEGV_MAPERR   1
#define SEGV_ACCERR   2
#define BUS_ADRALN    1
#define BUS_ADRERR    2
#define ILL_ILLOPC    1
#define FPE_FLTDIV    3
#define FPE_FLTOVF    4
#define FPE_FLTUND    5
#define FPE_FLTRES    6
#define FPE_FLTINV    7
typedef struct siginfo siginfo_t;
struct siginfo
{
  int   si_signo, si_code, si_errno;
  pid_t si_pid;
  uid_t si_uid;
  int   si_status;
  void *si_addr;
};
struct sigaction
{
  union
  {
    void (*handler)(int);
    void (*info)(int, siginfo_t *, void *);
  } function;
  sigset_t sa_mask;
  int      sa_flags;
};
#define sa_handler   function.handler
#define sa_sigaction function.info
int sigaction(int signum, const struct sigaction *action, struct sigaction *old);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *set);
int raise(int signum);
#endif
