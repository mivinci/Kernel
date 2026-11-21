#ifndef _LIBSYS_FORKSTATE_H
#define _LIBSYS_FORKSTATE_H
#include "fd.h"
#include "contextipc.h"
typedef struct Forkstate Forkstate;
struct Forkstate
{
  Fdtable   *fds;
  Filepool  *files;
  Conncache *connections;
  Rbank     *contexts;
};
/* Read-only local admission, not backend authority or permission to publish.
   The coordinator supplies prepared references and derives pins from typed
   continuations separately. Reserved creator/fd repair is still separate.
   Keep all inputs and transport records stable while checking and committing.
   Retained File tags/tokens must name the same inherited directory and Fix
   backend; another directory with the same Image identity is insufficient. */
isize sysforkstatecheck(const Forkstate *state, Connpeer parent, Connpeer child,
                        const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files,
                        u64 file_count, const Fixconn *connections, u64 connection_count);
/* Single protected child-local conversion, after removing inherited shared
   mappings separately. No signal delivery between admission and commit.
   Inputs and mutable tables must occupy disjoint storage. Creator reserves
   are not supported yet and reject before writes. Preserve fd contents and
   all parent transport records; convert Rctx before File/directory identity.
   This helper is single-use: a later entry needs the coordinator's completed
   marker, not another apply against already converted child state. */
isize sysforkstateapply(const Forkstate *state, Connpeer parent, Connpeer child,
                        const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files,
                        u64 file_count, const Fixconn *connections, u64 connection_count);
#endif
