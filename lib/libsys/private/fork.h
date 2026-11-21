#ifndef _LIBSYS_FORK_H
#define _LIBSYS_FORK_H
#include "contextipc.h"
#include "elf.h"

typedef struct Forkfix Forkfix;
struct Forkfix
{
  u64             address, capacity;
  void           *buffer;
  const Fixconn  *connections;
  u64             connection_count;
  const Resentry *resources;
  u64             resource_count;
};
/* Complete the local side of an already prepared fork. Caller owns the exact
   preparation and any backend tokens. Keep the bank/transport at the snapshot
   boundary and buffer disjoint from live state and the scratch mapping.
   Returns the published PID or a negative status; retained work can only be
   canceled. Backend cleanup remains with the caller. */
isize sysforkfinish(Elfwork *work, Rbank *bank, Connpeer parent, const Forkfix *fix);
#endif
