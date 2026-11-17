/* Trusted logical private-page branches; not user Handles or page-table policy. */
#ifndef _COW_H
#define _COW_H
#include <acct.h>
#ifndef COW_REFS
#define COW_REFS 8192
#endif
#ifndef COW_GENERATION_MAX
#define COW_GENERATION_MAX (~(u64) 0)
#endif
typedef struct Cowid   Cowid;
typedef struct Cowstat Cowstat;
typedef struct Cowpool Cowpool;
struct Cowid
{
  u64 generation;
  u32 index, reserved;
};
struct Cowstat
{
  Acctid acct;
  u64    pa;
  u32    branches, payer;
};
struct Cowpool
{
  u32 branches, groups, prepaid, retired;
};
STATIC_ASSERT(COW_REFS > 0 && COW_REFS <= 65536, cow_capacity);
void cowinit(void);
/* Success consumes a real owned zero-order block; failure changes no record. */
int cowcreate(Acctblk *owned, Cowid *out);
/* Prepay peer quota, physical capacity and its eventual independent group. */
int cowshare(Cowid source, Acctid target, Cowid *out);
/* Requires shared backing immutable across all CPU aliases, no concurrent
   mapping/branch mutation. Copies the entire page before publishing new pa.
   Does not update any PTE; callers must commit all logical aliases together. */
int  cowwrite(Cowid branch, u64 *pa);
int  cowdrop(Cowid branch);
int  cowstat(Cowid branch, Cowstat *out);
void cowpool(Cowpool *out);
int  cowcheck(void);
#endif
