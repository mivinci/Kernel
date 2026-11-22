#ifndef _FS_WALK_H
#define _FS_WALK_H
#include "path.h"

typedef struct Vnode    Vnode;
typedef struct Vmounts  Vmounts;
typedef struct Vwalk    Vwalk;
typedef struct Vwalkops Vwalkops;

enum
{
  VNODE_FILE = 1,
  VNODE_DIR,
  VNODE_LINK,
};
enum
{
  VWALK_MORE = 1,
  VWALK_WAIT,
};
struct Vnode
{
  u64 mount, number, generation;
  u32 kind;
};
struct Vwalkops
{
  isize (*hold)(void *, Vnode *);
  void (*drop)(void *, Vnode *);
  isize (*search)(void *, const Vnode *);
  isize (*lookup)(void *, const Vnode *, const char *, u32, Vnode **);
  isize (*parent)(void *, const Vnode *, Vnode **);
  isize (*readlink)(void *, const Vnode *, const char **, u64 *);
};
struct Vwalk
{
  Fspath          path;
  Pathpart        part;
  const Vwalkops *ops;
  const Vmounts  *mounts;
  void           *context;
  Vnode          *root, *current, *link;
  u32             phase, follow, searchlast;
};

int vnodesame(const Vnode *left, const Vnode *right);

/* The walk starts zeroed and stays at the same address while active. Hold
   and drop are synchronous and also pin and unpin the node's mount. Lookup
   and parent transfer one backend node reference on success. Search uses the
   original operation's credentials in context. A pending operation returns
   VWALK_WAIT and is resumed with the same arguments. The ready link text
   is borrowed only until the next backend call. */
isize vwalkinit(Vwalk *walk, const Vmounts *mounts, const Vwalkops *ops, void *context, Vnode *root,
                Vnode *start, const volatile char *path, u64 bytes, u32 follow);
/* Advance one step. Zero transfers the result reference. MORE can run
   again immediately, WAIT needs backend progress, and errors drop refs. */
isize vwalkstep(Vwalk *walk, Vnode **out);
/* After any pending backend operation has been cancelled or drained. */
void vwalkdrop(Vwalk *walk);
#endif
