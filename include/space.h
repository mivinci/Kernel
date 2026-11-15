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
  u32    state, refs, maps, busy;
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
/* Kernel root must stay alive; all mutations serialized, active roots cannot be mutated by this
 * bootstrap mechanism. */
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
/* Trusted kernel destination; precheck every source page before copying. */
int spaceread(Spaceid id, u64 address, void *target, u64 bytes);
/* Inactive root only, bounded records per call; 1 means more, 0 empty. */
int spaceclear(Spaceid id, u32 records);
int spaceunmap(Spaceid id, u64 address);
#endif
