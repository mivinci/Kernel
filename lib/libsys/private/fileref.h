#ifndef _LIBSYS_FILEREF_H
#define _LIBSYS_FILEREF_H
#include "conn.h"
#define SYSFILE_REFS         256
#define SYSFILE_GEN_MAX      (UINT64_MAX >> 8)
#define SYSFILE_REF_FREE     0
#define SYSFILE_REF_LIVE     1
#define SYSFILE_REF_CLOSING  2
#define SYSFILE_REF_RETIRED  3
#define SYSFILE_REF_RESERVED 4
typedef struct Fileref   Fileref;
typedef struct Filepool  Filepool;
typedef struct Fileclose Fileclose;
typedef struct Filestage Filestage;
typedef struct Filevalue Filevalue;
typedef struct Filefork  Filefork;
struct Filestage
{
  u64 files[2];
  u32 count, reserved;
};
struct Filevalue
{
  Conntag connection;
  u64     session, reference, token;
};
struct Filefork
{
  u64       file;
  Filevalue value;
  u32       aliases, pins;
};
STATIC_ASSERT(sizeof(Filefork) == 56, file_fork_plan_size);
struct Fileref
{
  u64     generation;
  Conntag connection;
  u64     session, reference, token;
  u32     aliases, pins, state, reserved;
  u64     close_sequence;
};
struct Filepool
{
  Connpeer self;
  u64      sequence;
  Fileref  entries[SYSFILE_REFS];
};
struct Fileclose
{
  u64 file, sequence;
};
STATIC_ASSERT(sizeof(Fileref) == 72, local_file_reference_size);
STATIC_ASSERT(sizeof(Fileclose) == 16, local_file_close_key);
STATIC_ASSERT(sizeof(Filestage) == 24 && sizeof(Filevalue) == 40, prospective_file_reference);
/* Private client-Image registry and prepaid last-close anchor. No backend
   permissions, offset or mutable File status is cached as authority here.
   Fresh preallocated storage only; caller excludes caught signals. self is
   the runtime's actual Image identity, never a way to relabel parent state.
   file IDs are private generation[55:0]/index[7:0], not kernel Handles. */
isize sysfilerefinit(Filepool *pool, Connpeer self);
isize sysfilerefcheck(const Filepool *pool, Connpeer self);
/* Preflight against the parent before any child conversion. Same plan rules
   as fork below, no mutation; already converted child state is stale here.
   Caller holds inputs stable across all admissions and the local commit. */
isize sysfilerefforkcheck(Filepool *pool, Connpeer parent, Connpeer child, const Filefork *entries,
                          u64 count);
/* Protected child-local conversion after the coordinator fixed all Rctx/File
   results and prepared exact target backend references/tokens. The plan owns
   no authority itself; caller verifies service identities and child claims.
   Preserve retained local IDs, reduce alias/pin counts only, rebind to the
   next directory generation at the same index. Drop other parent copies
   without CLOSE/unpin/ACK; their responsibility remains in the parent.
   No reserved creator may be retained through this live-reference path. */
isize sysfilereffork(Filepool *pool, Connpeer parent, Connpeer child, const Filefork *entries,
                     u64 count);
/* Reserve one creator result or both PIPE ends before remote submission.
   stage is initially zero; reserved IDs cannot be viewed/pinned/published as
   live references. Capacity failure changes neither generations nor outputs.
   commit validates the whole batch before consuming stage. closing=1 moves
   unpublished successful results directly to existing close responsibility,
   without ever making them live descriptors. Caller owns stable independent
   value storage, excludes caught signals, and has saved/proved original RPC
   finish plus exact backend identities. No IPC or fd publication here. */
isize sysfilerefreserve(Filepool *pool, Connpeer self, u32 count, Filestage *stage);
isize sysfilerefcommit(Filepool *pool, Connpeer self, Filestage *owned_stage,
                       const Filevalue *values, u32 closing);
/* Only before submission, after a known no-reference result, or after proven
   compensation. Unknown remote results never authorize releasing the reserve.
   Both IDs are validated before either is returned; generations never rewind. */
isize sysfilerefabort(Filepool *pool, Connpeer self, Filestage *owned_stage);
isize sysfilerefstaged(Filepool *pool, Connpeer self, const Filestage *stage);
/* Before connection teardown: 1 while any live/pending-close reference still
   uses this exact directory incarnation, 0 only when none remain. */
isize sysfilerefconnection(const Filepool *pool, Connpeer self, Conntag connection);
isize sysfilerefnew(Filepool *pool, Connpeer self, Conntag connection, u64 session, u64 reference,
                    u64 token, u64 *file);
isize sysfilerefview(Filepool *pool, Connpeer self, u64 file, Fileref *out);
isize sysfilerefalias(Filepool *pool, Connpeer self, u64 file);
isize sysfilerefdrop(Filepool *pool, Connpeer self, u64 file);
isize sysfilerefpin(Filepool *pool, Connpeer self, u64 file);
isize sysfilerefunpin(Filepool *pool, Connpeer self, u64 file);
/* Discover an unclaimed close with no pins for this exact connection, starting
   at the given slot index. No mutation; errors and exhaustion preserve out.
   Keep signal exclusion across discovery and claiming with sysfilerefclose. */
isize sysfilerefnextclose(const Filepool *pool, Connpeer self, Conntag connection, u32 minimum,
                          u64 *out);
/* Last descriptor is already detached, pins=0. Returns one persistent closing
   anchor exactly once; busy/unknown close keeps the slot, never resubmits.
   confirmed is ONLY after caller matched an actual backend close result or
   completed its explicit lifecycle recovery policy. It performs no RPC. */
isize sysfilerefclose(Filepool *pool, Connpeer self, u64 file, Fileclose *out);
/* Roll back a claim only when no operation has been prepared or submitted
   for it (or preparation has rolled back). The sequence is never reused. */
isize sysfilerefunclaimclose(Filepool *pool, Connpeer self, const Fileclose *key);
isize sysfilerefconfirmed(Filepool *pool, Connpeer self, const Fileclose *key);
#endif
