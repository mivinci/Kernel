/* Kernel-internal RAM backing and trusted MMIO views, not Local Handles. */
#ifndef _MEM_H
#define _MEM_H
#include <acct.h>
#ifndef MEMS
#define MEMS 8192
#endif
#ifndef BREFS
#define BREFS 65536
#endif
#define MEM_PAGES       16
#define MEM_DATA        1
#define MEM_LOADING     3
#define MEM_SEALED      4
#define MEM_SHARED      5
#define MEM_RAM         0
#define MEM_MMIO        1
#define MEM_IMMUTABLE   2
#define MEM_BOOT        3
#define MEM_FORK_SOURCE 1
#define MEM_FORK_VIEW   2
#define MEM_DENIED      -7
#define MEM_BUSY        -4
#define MEM_BAD         -1
#define MEM_LIMIT       -2
#define MEM_FULL        -3

typedef struct Memid    Memid;
typedef struct Mem      Mem;
typedef struct Blockref Blockref;
struct Memid
{
  u64 generation;
  u32 index, reserved;
};
struct Mem
{
  u64    generation, bytes;
  Acctid acct;
  /* state is content/seal provenance until final reap; managers==0 means
     retiring and prohibits acquiring any new manager/map/Op reference. */
  u32 head, pages, refs, managers, state, writes, execs, busy, kind, cowmaps;
  /* IMMUTABLE retains canonical code; a staged DATA fork view instead retains
     its original logical Mem until commit/last release. */
  Memid backing;
  u32   fork, adopted;
};
struct Blockref
{
  u64 pa;
  u32 next, order;
};
STATIC_ASSERT(MEMS > 0 && MEMS <= 8192, mem_capacity);
STATIC_ASSERT(BREFS > 0 && BREFS <= 65536, blockref_capacity);
STATIC_ASSERT(sizeof(Mem) <= 96, mem_budget);
STATIC_ASSERT(sizeof(Blockref) == 16, blockref_budget);
void meminit(void);
int  memcreate(Acctid acct, u64 bytes, Memid *out);
int  memcode(Acctid acct, u64 bytes, Memid *out);
/* Trusted exec loader. The caller verifies the exact preparation. */
int memexeccreate(Acctid acct, u64 ticket, u64 bytes, u32 code, Memid *out);
int memshared(Acctid acct, u64 bytes, Memid *out);
/* Trusted retained-package slice, pages counted from bootstore logical start.
   No physical allocation/charge, user Handle, write or execute authority. */
int memboot(Acctid acct, u32 first, u32 pages, Memid *out);
/* Trusted preparation: private DATA only, all writable source maps must be Space-protected.
   Target gets independent logical pages; existing read-only source maps stay.
   Output unchanged on failure; no user fork authority or PTE protection here. */
int memclone(Memid source, Acctid target, Memid *out);
/* Durable DATA preparation. One staged view locks its source against writes
   and further cloning, retaining the original even after its owners retire.
   Last child release drops its branches, undoes newly adopted raw pages, then
   releases the source. No caller-stack adoption mask or implicit parent grant.
   Space separately owns PTE protection/rollback. Output unchanged on failure. */
int memforkclone(Memid source, Acctid target, Memid *out);
/* Detach the journal without undoing COW. Ordinary nonstaged views are a no-op;
   a source lock is never a child view. No allocation or permission is granted. */
int memforkcommit(Memid view);
/* Trusted same-Set immutable fork view. Keeps the original physical payer,
   flattens view ancestry, allocates no RAM pages/COW credits. Source must be
   a retained SEALED view; output is unchanged on failure. */
int memimmutable(Memid source, Acctid target, Memid *out);
/* Trusted snapshot holds retained shared RAM or read-only boot backing for map
   construction, even after its last manager. Never private/MMIO pages. */
int memsharedhold(Memid source);
/* Trusted fork installation into the exact target logical view. The view must
   already be retained by a prepared map/owner; never a canonical parent DATA. */
int memforkhold(Memid view, Acctid target);
/* Space transaction only: all aliases/references must be frozen and validated.
   These accept a mapped/retiring private view but grant no new authority. */
int memcowshared(Memid id, u32 page);
int memcowbreak(Memid id, u32 page, u64 *pa);
/* Trusted logically-RW map installed entirely R/COW by Space. */
int memcowmapref(Memid id, Acctid target);
/* Trusted whole-page grant from the validated boot device inventory. */
int memdevice(Acctid acct, u32 device, Memid *out);
/* Trusted kernel source only; user copies require independent range/rights checks. */
int memwrite(Memid id, u64 offset, const void *source, u64 bytes);
int memseal(Memid id);
int memcanmap(Memid id, u64 prot);
int memmapref(Memid id, u64 prot, Acctid target);
int memunmapref(Memid id, u64 prot, Acctid target);
/* Space-owned count of logically writable maps whose complete PTE range is
   read-only/COW protected; no permissions or mapping references are granted. */
int memprotectref(Memid id, u32 enabled);
int memhold(Memid id);
int memfree(Memid id);
/* Mapping/Op references do not restore a released management reference. */
int memretain(Memid id);
int memrelease(Memid id);
int memstat(Memid id, Mem *out);
int mempage(Memid id, u32 page, u64 *pa);
#endif
