#ifndef _LIBSYS_FD_H
#define _LIBSYS_FD_H
#include <abi/boot.h>
#include "fileref.h"
#define SYSFD_FREE     0
#define SYSFD_RESERVED 1
#define SYSFD_LIVE     2
#define SYSFD_RETIRED  3
typedef struct Fdtag   Fdtag;
typedef struct Fdentry Fdentry;
typedef struct Fdview  Fdview;
typedef struct Fdtable Fdtable;
struct Fdtag
{
  u64 generation;
  u32 index, state;
};
struct Fdentry
{
  u64 generation, file, flags;
  u32 state, reserved;
};
struct Fdview
{
  Fdtag tag;
  u64   file, flags;
};
struct Fdtable
{
  Fdentry entries[BOOT_FD_COUNT];
};
STATIC_ASSERT(sizeof(Fdtable) == PAGE_SIZE, fd_table_page);
STATIC_ASSERT(sizeof(Fdentry) == 32 && sizeof(Fdview) == 32, fd_record_size);
STATIC_ASSERT(sizeof(Fdtag) == 16, fd_tag_size);
/* Private local table, not a backend File registry or public fd API. Callers
   exclude caught signals for these short operations; no function waits/does
   IPC, allocates memory, or resolves a service Handle. C storage must be valid.
   file is a nonzero opaque local File identity, never authority by itself. */
void  sysfdinit(Fdtable *table);
isize sysfdcheck(const Fdtable *table);
/* Read-only fork admission: every live fd must name exactly one retained
   File, whose alias count equals its descriptor count (including CLOEXEC).
   Pin-only Files are allowed. Reserved descriptors remain owned by their
   typed creator continuation; this check neither repairs nor drops them.
   Caller keeps both inputs stable under caught-signal exclusion. */
isize sysfdforkcheck(const Fdtable *table, const Filefork *entries, u64 count);
/* Observation borrows the live File reference only within caller protection.
   State is part of the tag: a reserved tag cannot later name a published fd. */
isize sysfdobserve(const Fdtable *table, u32 fd, Fdview *out);
isize sysfdreserve(Fdtable *table, u32 minimum, Fdtag *out);
/* Successful publication/replacement moves one caller-prepared reference to
   the table. A failure leaves the input reference owned by the caller. */
isize sysfdpublish(Fdtable *table, Fdtag reserved, u64 file, u64 flags);
isize sysfdcancel(Fdtable *table, Fdtag reserved);
/* Success moves any old live reference to out for prepaid caller cleanup.
   out must not overlap table and is unchanged on failure. Callers prepare all
   source pins/cleanup capacity before exchange; same-fd dup2 is handled above
   this layer. A source fd number is never re-read here after a remote step. */
isize sysfdreplace(Fdtable *table, Fdtag expected, u64 file, u64 flags, Fdview *out);
isize sysfddetach(Fdtable *table, Fdtag expected, Fdview *out);
isize sysfdsetflags(Fdtable *table, Fdtag expected, u64 flags);
/* Duplicate into the lowest free fd at or above minimum; return its number.
   Caller excludes caught signals across both disjoint tables. The source's
   File and prepaid close anchor are shared; the new fd starts without CLOEXEC. */
isize sysfddup(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 minimum);
/* Same protection as sysfddup. Success drops the displaced alias into its
   prepaid File record and writes the newly closing File ID, or zero if no
   last alias was displaced. The output disjoint from both tables is unchanged
   on failure. Close processing uses the File ID, never the target fd. */
isize sysfddup2(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 target,
                u64 *closing);
#endif
