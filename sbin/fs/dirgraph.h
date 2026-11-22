#ifndef _FS_DIRGRAPH_H
#define _FS_DIRGRAPH_H
#include "geometry.h"

typedef struct Dirnode  Dirnode;
typedef struct Dirgraph Dirgraph;

struct Dirnode
{
  u32 generation, kind, links;
};

struct Dirgraph
{
  const Dirnode *nodes;
  u32            parent[FS_INODES], incoming[FS_INODES];
  u8             reach[FS_INODES];
  u32            next;
  isize          failed;
};

/* Supply an immutable, decoded FS_INODES table from the stable home view.
   Kind zero denotes a free slot. A fresh graph starts with only nodes set. */
/* Record each live, ordinary name once, after record and name validation.
   Special entries and free records are handled by the directory reader. */
isize dirgraphedge(Dirgraph *, u32 directory, u32 target, u32 generation, u32 kind);
/* After all directories have been read, seal input and check a batch.
   Returns one when the whole graph has been checked, zero to continue. */
isize dirgraphstep(Dirgraph *);
#endif
