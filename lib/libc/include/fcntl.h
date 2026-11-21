#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H
#include <abi/boot.h>

#define F_DUPFD    0
#define F_GETFD    1
#define F_SETFD    2
#define FD_CLOEXEC BOOT_FD_CLOEXEC

int fcntl(int fd, int cmd, ...);
#endif
