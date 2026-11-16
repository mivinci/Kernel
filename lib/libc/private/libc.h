#ifndef _LIBC_PRIVATE_H
#define _LIBC_PRIVATE_H
#include <sys/boot.h>
typedef void (*Exitfn)(void);
isize         libcenv(const Boot *boot, int *argc, char ***argv);
const Boot   *libcboot(void);
void          libcexitinit(Exitfn finalizer);
NORETURN void libcstart(const Boot *boot, int (*entry)(int, char **));
#endif
