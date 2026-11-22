#ifndef _UNISTD_H
#define _UNISTD_H
#include <types.h>
#include <sys/types.h>
#define _SC_PAGESIZE  1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define SEEK_SET      0
#define SEEK_CUR      1
#define SEEK_END      2
extern char **environ;
extern char  *optarg;
extern int    opterr, optind, optopt;
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
int           pipe(int fds[2]);
int           close(int fd);
int           isatty(int fd);
int           getopt(int argc, char *const argv[], const char *optstring);
ssize_t       read(int fd, void *buffer, size_t count);
ssize_t       write(int fd, const void *buffer, size_t count);
ssize_t       pread(int fd, void *buffer, size_t count, off_t offset);
ssize_t       readlink(const char *path, char *buffer, size_t size);
ssize_t       pwrite(int fd, const void *buffer, size_t count, off_t offset);
off_t         lseek(int fd, off_t offset, int whence);
int           fsync(int fd);
int           fdatasync(int fd);
int           chdir(const char *path);
char         *getcwd(char *buffer, size_t size);
long          sysconf(int name);
NORETURN void _exit(int status);
#endif
