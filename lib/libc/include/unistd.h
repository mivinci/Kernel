#ifndef _UNISTD_H
#define _UNISTD_H
#include <types.h>
#include <sys/types.h>
extern char **environ;
pid_t         getpid(void);
pid_t         getppid(void);
NORETURN void _exit(int status);
#endif
