#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H
#include <abi/boot.h>
#include <proto/file.h>
#include <sys/types.h>

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC      BOOT_FD_CLOEXEC
#define O_RDONLY        FS_OPEN_RDONLY
#define O_WRONLY        FS_OPEN_WRONLY
#define O_RDWR          FS_OPEN_RDWR
#define O_ACCMODE       FS_OPEN_ACCMODE
#define O_CREAT         FS_OPEN_CREAT
#define O_EXCL          FS_OPEN_EXCL
#define O_TRUNC         FS_OPEN_TRUNC
#define O_APPEND        FS_OPEN_APPEND
#define O_NONBLOCK      FS_OPEN_NONBLOCK
#define O_CLOEXEC       FS_OPEN_CLOEXEC

int fcntl(int fd, int cmd, ...);
int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
#endif
