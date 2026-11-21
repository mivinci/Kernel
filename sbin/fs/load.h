#ifndef _FS_LOAD_H
#define _FS_LOAD_H
#include <types.h>
#include <abi/base.h>

#define LOAD_SLOTS 32

typedef struct Loadfile  Loadfile;
typedef struct Loadslot  Loadslot;
typedef struct Loadtable Loadtable;
struct Loadfile
{
  u64 inode, generation, version, length;
};
struct Loadslot
{
  Loadfile file;
  u64      id, image, deadline, read;
  Handle   pid;
  isize    stopped;
};
struct Loadtable
{
  Loadslot slots[LOAD_SLOTS];
  u64      serial;
};

/* Zero-initialized per FS image, serialized by the FS owner. Pass only a
   resolved, authorized file and the native source identity. The resolver and
   the mutation/reclaim paths must use the same table and inode namespace. */
isize loadopen(Loadtable *table, const Loadfile *file, Handle pid, u64 image, u64 now, u64 deadline,
               u64 *session);
/* One read in flight per session. Finish after all backing accesses drain,
   before publishing staged output; a negative result forbids publication. */
isize loadread(Loadtable *table, u64 session, Handle pid, u64 image, const Loadfile *file,
               u64 offset, u64 bytes, u64 now, u64 *ticket);
isize loadstatus(Loadtable *table, u64 session, u64 ticket, u64 now);
isize loadfinish(Loadtable *table, u64 session, u64 ticket, u64 now);
isize loadclose(Loadtable *table, u64 session, Handle pid, u64 image);
void  loadsweep(Loadtable *table, u64 now);
void  loadretire(Loadtable *table, Handle pid, u64 image);
/* Hold across content changes or inode reclamation, including deferred IO;
   renaming a path without changing or reclaiming its inode needs no guard. */
isize loadwrite(Loadtable *table, u64 inode, u64 *guard);
isize loadunwrite(Loadtable *table, u64 guard);
#endif
