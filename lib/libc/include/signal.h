#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H
#include <sys/types.h>
#include <abi/signal.h>
#define SIGHUP      SIGNAL_HUP
#define SIGINT      SIGNAL_INT
#define SIGQUIT     SIGNAL_QUIT
#define SIGILL      SIGNAL_ILL
#define SIGABRT     SIGNAL_ABRT
#define SIGBUS      SIGNAL_BUS
#define SIGFPE      SIGNAL_FPE
#define SIGKILL     SIGNAL_KILL
#define SIGUSR1     SIGNAL_USR1
#define SIGSEGV     SIGNAL_SEGV
#define SIGUSR2     SIGNAL_USR2
#define SIGPIPE     SIGNAL_PIPE
#define SIGALRM     SIGNAL_ALRM
#define SIGTERM     SIGNAL_TERM
#define SIGCHLD     SIGNAL_CHLD
#define SIGCONT     SIGNAL_CONT
#define SIGSTOP     SIGNAL_STOP
#define SIGTSTP     SIGNAL_TSTP
#define SIGTTIN     SIGNAL_TTIN
#define SIGTTOU     SIGNAL_TTOU
#define SIGWINCH    SIGNAL_WINCH
#define SIG_BLOCK   SIGNAL_BLOCK
#define SIG_UNBLOCK SIGNAL_UNBLOCK
#define SIG_SETMASK SIGNAL_SETMASK
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
