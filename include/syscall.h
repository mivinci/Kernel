#ifndef _SYSCALL_H
#define _SYSCALL_H
#include <hal/context.h>
NORETURN void syscall(Hctx *context);
#endif
