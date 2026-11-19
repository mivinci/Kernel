#ifndef _LIBSYS_FILEREF_H
#define _LIBSYS_FILEREF_H
#include "conn.h"
#define SYSFILE_REFS        256
#define SYSFILE_GEN_MAX     (UINT64_MAX >> 8)
#define SYSFILE_REF_FREE    0
#define SYSFILE_REF_LIVE    1
#define SYSFILE_REF_CLOSING 2
#define SYSFILE_REF_RETIRED 3
typedef struct Fileref   Fileref;
typedef struct Filepool  Filepool;
typedef struct Fileclose Fileclose;
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
/* Private client-Image registry and prepaid last-close anchor. No backend
   permissions, offset or mutable File status is cached as authority here.
   Fresh preallocated storage only; caller excludes caught signals. self is
   the runtime's actual Image identity, never a way to relabel parent state.
   file IDs are private generation[55:0]/index[7:0], not kernel Handles. */
isize sysfilerefinit(Filepool *pool, Connpeer self);
isize sysfilerefcheck(const Filepool *pool, Connpeer self);
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
/* Last descriptor is already detached, pins=0. Returns one persistent closing
   anchor exactly once; busy/unknown close keeps the slot, never resubmits.
   confirmed is ONLY after caller matched an actual backend close result or
   completed its explicit lifecycle recovery policy. It performs no RPC. */
isize sysfilerefclose(Filepool *pool, Connpeer self, u64 file, Fileclose *out);
isize sysfilerefconfirmed(Filepool *pool, Connpeer self, const Fileclose *key);
#endif
