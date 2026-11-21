#ifndef _LIBSYS_FORK_H
#define _LIBSYS_FORK_H
#include "forkstate.h"
#include "elf.h"

typedef struct Forkfix    Forkfix;
typedef struct Forkdone   Forkdone;
typedef struct Forkdetach Forkdetach;
typedef struct Forkresume Forkresume;
struct Forkdetach
{
  Connwork *const *works;
  u64              count;
  Handle           space;
};
struct Forkdone
{
  Connpeer   child, parent;
  const Fix *fix;
};
struct Forkfix
{
  u64             address, capacity;
  void           *buffer;
  const Fixconn  *connections;
  u64             connection_count;
  const Resentry *resources;
  u64             resource_count;
};
struct Forkresume
{
  Forkstate         state;
  u64               available;
  const Filefork   *files;
  u64               file_count;
  const Forkdetach *detach;
  Handle            space;
};
/* Complete the local side of an already prepared fork. Caller owns the exact
   preparation and any backend tokens. Keep the bank/transport at the snapshot
   boundary and buffer disjoint from live state and the scratch mapping.
   Returns the published PID or a negative status; retained work can only be
   canceled. Backend cleanup remains with the caller. */
isize sysforkfinish(Elfwork *work, Rbank *bank, Connpeer parent, const Forkfix *fix);
/* Both child entries use the same private, initially zeroed marker. This
   detaches all inherited transport aliases listed by the caller, using the
   validated child SELF_SPACE. Null detach means no inherited aliases.
   Marker and inputs must be disjoint from the mutable tables. */
isize sysforkrepair(Forkdone *done, const Forkstate *state, Connpeer parent, const Forkret *result,
                    u64 available, const Filefork *files, u64 file_count, const Forkdetach *detach);
/* Keep the plan and its referenced storage stable through the child return.
   Space is the inherited SELF_SPACE; Fix starts at its own mapping base.
   The runtime consumes Fix before exposing the child PID, so out->fix is null. */
isize sysforkresume(Handle authority, u64 reserve, const Forkresume *plan, Forkret *out);
/* Called with caught signals masked when a bound bank still names the parent. */
isize sysforksignal(Rbank *bank, const void *trap, const void *outer);
#endif
