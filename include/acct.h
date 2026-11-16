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
  u64 generation, limit, exec_limit, used, reserved;
  u32 set, state, refs, bound;
};
struct Acctblk
{
  Pgblk  block;
  Acctid acct;
};
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
int acctfree(Acctblk block);
int acctcheck(void);
#endif
