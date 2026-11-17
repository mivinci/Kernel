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
#define MEM_PAGES   16
#define MEM_DATA    1
#define MEM_LOADING 3
#define MEM_SEALED  4
#define MEM_SHARED  5
#define MEM_RAM     0
#define MEM_MMIO    1
#define MEM_DENIED  -7
#define MEM_BUSY    -4
#define MEM_BAD     -1
#define MEM_LIMIT   -2
#define MEM_FULL    -3

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
  u32 head, pages, refs, managers, state, writes, execs, busy, kind;
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
int  memshared(Acctid acct, u64 bytes, Memid *out);
/* Trusted preparation: private DATA only, source must have no writable maps.
   Target gets independent logical pages; existing read-only source maps stay.
   Output unchanged on failure; no user fork authority or PTE protection here. */
int memclone(Memid source, Acctid target, Memid *out);
/* Trusted whole-page grant from the validated boot device inventory. */
int memdevice(Acctid acct, u32 device, Memid *out);
/* Trusted kernel source only; user copies require independent range/rights checks. */
int memwrite(Memid id, u64 offset, const void *source, u64 bytes);
int memseal(Memid id);
int memcanmap(Memid id, u64 prot);
int memmapref(Memid id, u64 prot, Acctid target);
int memunmapref(Memid id, u64 prot, Acctid target);
int memhold(Memid id);
int memfree(Memid id);
/* Mapping/Op references do not restore a released management reference. */
int memretain(Memid id);
int memrelease(Memid id);
int memstat(Memid id, Mem *out);
int mempage(Memid id, u32 page, u64 *pa);
#endif
