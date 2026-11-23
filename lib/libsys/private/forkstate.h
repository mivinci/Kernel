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
/* Derive child File values from the copied fd table and prepared Fix peers.
   This path retains fd aliases only; continuation pins need their own plan. */
isize sysforkfiles(const Forkstate *state, Connpeer parent, const Fixconn *connections,
                   u64 connection_count, Filefork *files, u64 capacity);
/* Read-only local admission, not backend authority or permission to publish.
   The coordinator supplies prepared references and derives pins from typed
   continuations separately. Reserved creator/fd repair is still separate.
   Keep all inputs and transport records stable while checking and committing.
   Retained File tags/tokens must name the same inherited directory and Fix
   backend; another directory with the same Image identity is insufficient. */
isize sysforkstatecheck(const Forkstate *state, Connpeer parent, Connpeer child,
                        const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files,
                        u64 file_count, const Fixconn *connections, u64 connection_count);
/* Full read-only admission including storage separation and creator reserves. */
isize sysforkstateadmit(const Forkstate *state, Connpeer parent, Connpeer child,
                        const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files,
                        u64 file_count, const Fixconn *connections, u64 connection_count);
/* Single protected child-local conversion, after removing inherited shared
   mappings. Preserve fd contents and parent transport records; convert Rctx
   before File/directory identity. Re-entry requires a completed marker. */
isize sysforkstateapply(const Forkstate *state, Connpeer parent, Connpeer child,
                        const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files,
                        u64 file_count, const Fixconn *connections, u64 connection_count);
#endif
