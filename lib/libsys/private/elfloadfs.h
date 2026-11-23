#ifndef _LIBSYS_ELFLOADFS_H
#define _LIBSYS_ELFLOADFS_H
#include "elf.h"
#include "operation.h"

typedef struct Elfloadfs Elfloadfs;
struct Elfloadfs
{
  Connwork   *work;
  Conncache  *cache;
  u64         session, identity[4], owner;
  u32         layer;
  Loadrequest pending;
  Handle      submitted;
  isize       business;
};
/* Caller owns the connection and version-pinned session through all reads
   and any outstanding drain. Start with pending and submitted zero. */
isize elfloadfsread(void *context, u64 offset, void *destination, u64 bytes);
/* Cancel an outstanding read and discard its result after FINISH. Nonblocking;
   retry WOULD_BLOCK without releasing the connection, slot or session. */
isize elfloadfsdrain(Elfloadfs *source);
#endif
