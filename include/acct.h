/* Internal accounting. Caller serializes mutations; no userspace authority here. */
#ifndef _ACCT_H
#define _ACCT_H
#include <page.h>
#define AC_SETS    16
#define AC_ACCTS   256
#define AC_FREE    0
#define AC_ACTIVE  1
#define AC_CLOSING 2
#define AC_BAD     -1
#define AC_LIMIT   -2
#define AC_FULL    -3
#define AC_BUSY    -4
#define AC_NOMEM   -5

typedef struct Setid   Setid;
typedef struct Acctid  Acctid;
typedef struct Set     Set;
typedef struct Acct    Acct;
typedef struct Acctblk Acctblk;
typedef struct Acctcow Acctcow;
struct Setid
{
  u64 generation;
  u32 index, reserved;
};
struct Acctid
{
  u64 generation;
  u32 index, reserved;
};
struct Set
{
  u64 generation, limit, exec_limit, charged;
  u32 instances, max_instances, refs, state, leases;
};
struct Acct
{
  u64 generation, limit, exec_limit, used, reserved, cow;
  u32 set, state, refs, bound;
};
struct Acctblk
{
  Pgblk  block;
  Acctid acct;
};
/* Owns one stable Acct reference plus physical capacity; zero initialize,
   never copy a live record, release before the owning COW branch is reused. */
struct Acctcow
{
  Acctid   acct;
  Pgcredit physical;
};
/* A reserve commits both quota and physical zero-order capacity or neither.
   It accepts active accounts only; existing commitments can finish closing.
   No user authority, mappings, content copy or COW branch validation here. */
int acctcowreserve(Acctid id, u64 pages, Acctcow *out);
int acctcowcancel(Acctcow *credit, u64 pages);
int acctcowalloc(Acctcow *credit, Acctblk *out);
/* Departing payer: transfer one actual zero-order block to a same-Set branch
   with a prior reservation. Consumes one credit and updates the owned record.
   Does not copy data or permit arbitrary cross-Set gifts. */
int acctcowmove(Acctblk *block, Acctcow *credit);
/* Original payer writes: redeem a peer credit for its replacement page and
   move the old page charge to that peer. Both owned records change together.
   No temporary extra payer quota; raw new bytes must be copied before publish.
   old/out/credit storage must be disjoint, caller serializes all mutations. */
int acctcowreplace(Acctblk *old, Acctcow *credit, Acctblk *out);
STATIC_ASSERT(sizeof(Acctcow) == 24, acct_cow_record);
STATIC_ASSERT(sizeof(Set) <= 64, set_budget);
STATIC_ASSERT(sizeof(Acct) <= 64, acct_budget);

/* Create/close are bootstrap/policy operations, not delegated process calls. */
int setcreate(u64 limit, u64 peak, u32 max_instances, Setid *out);
int setclose(Setid id);
/* Life admission is separate from Acct lifetime and must be rolled back by caller. */
int setenter(Setid id);
int setleave(Setid id);
int setstat(Setid id, Set *out);
/* Strong policy leases; boot keeps the original Set owner reference. */
int sethold(Setid id);
int setdrop(Setid id);
u64 settag(Setid id);
int setlookup(u64 tag, Setid *out);
int acctcreate(Setid set, u64 limit, u64 peak, Acctid *out);
int accthold(Acctid id);
int acctdrop(Acctid id);
int acctclose(Acctid id);
/* Unbind releases the original process/construction reference, only after close. */
int acctunbind(Acctid id);
int acctstat(Acctid id, Acct *out);
/* Stable identity only: no authority and no retained accounting reference. */
u64 accttag(Acctid id);
int acctlookup(u64 tag, Acctid *out);
/* Existing charge/reservations must fit; this does not change Set limits. */
int acctbudget(Acctid id, u64 limit, u64 peak);
/* Ordinary admission only. Peak/COW capacity is not implemented by these helpers. */
int acctreserve(Acctid id, u64 pages);
int acctconvert(Acctid id, u64 pages);
int acctrelease(Acctid id, u64 used, u64 reserved);
/* One block owns one stable Acct reference; successful free consumes the record.
   This internal record is not a user handle or a per-allocation generation.
   Raw pages remain unzeroed/unmapped; callers enforce sharing and DMA lifetime.
   Output is unchanged on failure. */
int acctalloc(Acctid id, u32 order, const Pglim *lim, Acctblk *out);
/* One live ticket per Acct. Prep must retain and validate its own identity
   before using the ticket, then end it on publication or cancellation.
   Ending a ticket revokes admission; it does not release existing charges. */
int acctexecbegin(Acctid id, u64 *ticket);
int acctexecvalid(Acctid id, u64 ticket);
int acctexecalloc(Acctid id, u64 ticket, u32 order, const Pglim *lim, Acctblk *out);
int acctexecend(Acctid id, u64 ticket);
int acctfree(Acctblk block);
int acctcheck(void);
#endif
