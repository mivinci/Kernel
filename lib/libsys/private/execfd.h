#ifndef _LIBSYS_EXECFD_H
#define _LIBSYS_EXECFD_H
#include "fd.h"
#include <proto/file.h>

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
/* Pack one exact backend's EXEC_PREPARE from the stable capture. Each fd
   gets its own wire row; mixed CLOEXEC aliases keep their original flags.
   Output is private staging, disjoint from capture/pool. Returns entry count
   or negative status. No match produces an empty manifest, not a request. */
isize sysexecfdmanifest(const Execfds *capture, Connpeer backend, u64 target_image, u64 deadline,
                        u64 args[8], Fileinherit *rows, u64 capacity);
/* Pack surviving aliases for one prepared backend. call is the target's
   local CALL, and token belongs to this exact backend preparation. */
isize sysexecfdboot(const Execfds *capture, Connpeer backend, Handle call, u64 token, Bootfd *rows,
                    u64 capacity);
#endif
