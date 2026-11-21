#ifndef _UNISTD_H
#define _UNISTD_H
#include <types.h>
#include <sys/types.h>
#define _SC_PAGESIZE  1
#define _SC_PAGE_SIZE _SC_PAGESIZE
extern char **environ;
pid_t         getpid(void);
pid_t         getppid(void);
uid_t         getuid(void);
uid_t         geteuid(void);
gid_t         getgid(void);
gid_t         getegid(void);
pid_t         getpgrp(void);
pid_t         getpgid(pid_t pid);
pid_t         getsid(pid_t pid);
int           setpgid(pid_t pid, pid_t pgid);
pid_t         setsid(void);
int           dup(int fd);
int           dup2(int fd, int target);
long          sysconf(int name);
NORETURN void _exit(int status);
#endif
