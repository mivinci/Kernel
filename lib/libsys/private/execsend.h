#ifndef _LIBSYS_EXECSEND_H
#define _LIBSYS_EXECSEND_H
#include "execfd.h"
#include "operation.h"

typedef struct Execsend Execsend;
struct Execsend
{
  Opkey       key;
  Fileinherit rows[BOOT_FD_COUNT];
};

/* Prepare the slot for the exact connection, using an independently owned
   File pin on it. Caller excludes caught signals until staging completes.
   Success moves the pin into the Op; failure before submission leaves it
   with the caller. Submit and reap with the ordinary Rctx/Op path, holding
   capture until the backend preparation/compensation has been resolved. */
isize sysexecfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context,
                     u64 owner, u64 target_image, u64 deadline, u64 *owned_file, Execsend *send);
#endif
