#ifndef _FS_DIRGRAPH_H
#define _FS_DIRGRAPH_H
#include "geometry.h"

typedef struct Dirnode   Dirnode;
typedef struct Dirgraph  Dirgraph;
typedef struct Dirrecord Dirrecord;
typedef isize (*Dirdecode)(const u8 *, Dirrecord *);

/* Normalized values from the 16-byte record header, not disk layout. */
struct Dirrecord
{
  u32 inode, generation, reclen, namelen, kind;
};

struct Dirnode
{
  u32 generation, kind, links;
};

struct Dirgraph
{
  const Dirnode *nodes;
  u32            parent[FS_INODES], incoming[FS_INODES];
  u16            dotdot[FS_INODES];
  u8             reach[FS_INODES], dot[FS_INODES];
  u32            next;
  isize          failed;
};

/* Supply an immutable, decoded FS_INODES table from the stable home view.
   Kind zero denotes a free slot. A fresh graph starts with only nodes set. */
/* Record each live, ordinary name once, after record and name validation.
   Special entries and free records are handled by the directory reader. */
isize dirgraphedge(Dirgraph *, u32 directory, u32 target, u32 generation, u32 kind);
/* Decode only a complete header. The walker checks bounds before inspecting
   the name and does not advance past malformed records. */
isize dirgraphblock(Dirgraph *, u32 directory, const u8 *, u32 bytes, Dirdecode);
/* After all directories have been read, seal input and check a batch.
   Returns one when the whole graph has been checked, zero to continue. */
isize dirgraphstep(Dirgraph *);
#endif
