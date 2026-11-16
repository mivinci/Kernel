#ifndef _UNISTD_H
#define _UNISTD_H
#include <types.h>
extern char **environ;
NORETURN void _exit(int status);
#endif
