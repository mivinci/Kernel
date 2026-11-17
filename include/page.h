/* Internal physical allocator; callers serialize access before scheduling exists. */
#ifndef _PAGE_H
#define _PAGE_H
#include <dtb.h>
#define PG_ORDER 16
#define PG_NONE  0xffffffffU

typedef struct Page     Page;
typedef struct Pgblk    Pgblk;
typedef struct Pglim    Pglim;
typedef struct Pgstat   Pgstat;
typedef struct Pgcredit Pgcredit;

struct Page
{
  u32 state, order, head, prev, next, region;
  u64 owner;
};
struct Pgblk
{
  u64 pa;
  u32 order, reserved;
};
/* Owned internal reservation, zero-initialize once; never copy a live credit.
   The caller serializes mutations and releases it before its owner is reused. */
struct Pgcredit
{
  u64 pages;
};
struct Pglim
{
  u64 minpa, maxpa_exclusive, align;
};
struct Pgstat
{
  u64 total, managed, free, allocated, protected;
  u64 blocks[PG_ORDER + 1];
};
enum Pgerror
{
  PG_BAD = -1,
  PG_NOMEM = -2,
  PG_BUSY = -3
};
STATIC_ASSERT(sizeof(Page) == 32, page_size);

/* Metadata covers every RAM page (including reserved pages), never MMIO holes.
   'pages' is a caller-owned reserved array; init does not allocate physical pages.
   Each usable interval must already exclude metadata and all boot reservations. */
int pginit(const Dtinfo *info, Page *pages, u32 capacity, u64 lowwater);
int pgalloc(u32 order, const Pglim *lim, Pgblk *out);
int pgfree(Pgblk blk);
/* Capacity for unrestricted zero-order COW faults, separate from lowwater.
   No physical block/address is promised. Reservation changes are atomic. */
int pgreserve(Pgcredit *credit, u64 pages);
int pgcancel(Pgcredit *credit, u64 pages);
int pgredeem(Pgcredit *credit, Pgblk *out);
u64 pgreserved(void);
/* Nonzero owners protect accounted blocks from raw pgfree or a wrong account. */
int pgclaim(Pgblk blk, u64 owner);
int pgrelease(Pgblk blk, u64 owner);
/* Trusted COW charge transfer; exact allocated head and old owner required. */
int  pgtransfer(Pgblk blk, u64 oldowner, u64 newowner);
int  pgowned(Pgblk blk, u64 owner);
void pgstat(Pgstat *out);
/* Boot/host diagnostic: inspect all metadata/free lists; no mutation. */
int pgcheck(void);
/* One-way bootstrap relocation of the reserved metadata access pointer. */
void pgrelocate(u64 delta);
#endif
