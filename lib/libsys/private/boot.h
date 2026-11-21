#ifndef _LIBSYS_BOOT_H
#define _LIBSYS_BOOT_H
#include <abi/boot.h>
/* Syntax only. The caller checks the table's bounds; these records do not
   establish the type, rights or target of the named local resource. */
int sysbootfds(const Bootfd *fds, u64 count);
#endif
