/* User address-space ownership; Local authorization is layered separately. */
#ifndef _SPACE_H
#define _SPACE_H
#include <acct.h>
#include <mem.h>
#include <hal/vm.h>
#include <prepid.h>
#define SPACES      256
#define SP_BAD      -1
#define SP_FULL     -3
#define SP_BUSY     -4
#define SP_CONFLICT -6
#ifndef MMAPS
#define MMAPS 8192
#endif

typedef struct Spaceid Spaceid;
typedef struct Space   Space;
typedef struct Mmap    Mmap;
struct Spaceid
{
  u64 generation;
  u32 index, reserved;
};
struct Space
{
  u64    generation, root;
  Acctid acct;
  u64    pages;
  u32    state, refs, maps, busy, mapcount;
};
struct Mmap
{
  u64    address, length, offset;
  Memid  mem;
  u32    next, prot;
  Prepid prep;
};
STATIC_ASSERT(MMAPS > 0 && MMAPS <= 8192, mmap_capacity);
STATIC_ASSERT(sizeof(Mmap) <= 64, mmap_budget);
STATIC_ASSERT(sizeof(Space) <= 64, space_budget);
/* Kernel root stays alive; mutations are serialized. Mapping/clone/unmap
   require inactive roots. Owned kernel-write preparation can update only
   user DATA leaves of the installed writer root without leaving the kernel. */
int spaceinit(const Hvs *kernel);
int spaceactivate(Spaceid id);
int spacedeactivate(void);
int spacecreate(Acctid acct, Spaceid *out);
int spacehold(Spaceid id);
int spacedrop(Spaceid id);
int spacedestroy(Spaceid id);
int spacestat(Spaceid id, Space *out);
u32 spacelookup(Spaceid id, u64 va, Hpte *out);
int spaceloadmap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot, Prepid prep);
int spacemap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length, u64 prot);
/* Trusted inactive-space preparation: protect all existing logical RW aliases
   of this private Mem across spaces, clone, and undo new protections on error.
   Process admission and Local/resource inheritance are separate. */
int spaceclone(Memid source, Acctid target, Memid *out);
/* Trusted child/private mapping: logical RW, initially hardware R/COW. */
int spacecowmap(Spaceid id, Memid mem, u64 offset, u64 address, u64 length);
/* Inactive spaces only. Resolve exactly a COW-marked logical-W address and
   commit that logical page to every alias (R aliases remain R). The caller
   separately owns the complete saved user context and resumes it on success. */
int spaceresolve(Spaceid id, u64 address);
/* Trusted kernel destination; precheck every source page before copying. */
int spaceread(Spaceid id, u64 address, void *target, u64 bytes);
/* Trusted immutable kernel source. Precheck all output/aliases, prepare COW,
   then copy bytes. IRQs masked/no checkpoint; writer VM_BUSY owns the current
   user root and no target user VA is accessed during leaf replacement. */
int spacewrite(Spaceid id, u64 address, const void *data, u64 bytes);
/* Full input/output/physical-overlap preflight before COW. Reacquire both
   PA snapshots after separation; SP_CONFLICT means physical overlap. */
int spacecopy(Spaceid id, u64 destination, u64 source, u64 bytes);
/* Inactive root only, bounded records per call; 1 means more, 0 empty. */
int spaceclear(Spaceid id, u32 records);
int spaceunmap(Spaceid id, u64 address);
#endif
