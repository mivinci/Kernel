#ifndef _LIBSYS_EXECFD_H
#define _LIBSYS_EXECFD_H
#include "fd.h"

typedef struct Execfd  Execfd;
typedef struct Execfds Execfds;
struct Execfd
{
  Connpeer backend;
  u64      file, session, reference;
  u32      fd, flags;
};
struct Execfds
{
  Execfd    entries[BOOT_FD_COUNT];
  Filepool *pool;
  Connpeer  self;
  u32       count, ready;
};
/* Caller supplies zero-initialized private storage and excludes caught signals
   for each call. Capture records every live fd, including CLOEXEC, and pins
   its exact File. Group aliases by file in the eventual service plan; a File
   is CLOEXEC-only if all of its captured descriptors are marked. No IPC. */
isize sysexecfds(Execfds *out, const Fdtable *fds, Filepool *pool, Conncache *connections,
                 Connpeer self);
/* Only after consumers are done with the capture. Unpin does not close or
   restore descriptors; the existing last-close anchor owns that work. */
isize sysexecfdrelease(Execfds *capture);
#endif
