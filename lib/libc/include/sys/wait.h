#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>
#include <abi/proc.h>
#define WNOHANG              WAIT_WNOHANG
#define WUNTRACED            WAIT_WUNTRACED
#define WCONTINUED           WAIT_WCONTINUED
#define WIFEXITED(status)    (((unsigned int) (status) & 0x7fU) == 0)
#define WEXITSTATUS(status)  (((unsigned int) (status) >> 8) & 0xffU)
#define WIFSIGNALED(status)  ((((unsigned int) (status) & 0x7fU) - 1U) < 0x7eU)
#define WTERMSIG(status)     ((unsigned int) (status) & 0x7fU)
#define WIFSTOPPED(status)   (((unsigned int) (status) & 0xffU) == 0x7fU)
#define WSTOPSIG(status)     (((unsigned int) (status) >> 8) & 0xffU)
#define WIFCONTINUED(status) ((unsigned int) (status) == 0xffffU)
pid_t wait(int *status);
pid_t waitpid(pid_t selector, int *status, int options);
#endif
