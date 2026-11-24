#include <stdlib.h>
#include <unistd.h>

extern void __sys_exit(unsigned long) __attribute__((noreturn));

void
_Exit(int status)
{
  __sys_exit((unsigned int) status & 255);
}

void _exit(int status) __attribute__((noreturn, alias("_Exit")));
