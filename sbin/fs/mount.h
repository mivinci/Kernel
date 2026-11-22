#ifndef _FS_MOUNT_H
#define _FS_MOUNT_H
#include "walk.h"

#define VMOUNT_COUNT 16

typedef struct Vmount Vmount;
struct Vmount
{
  Vnode *root, *point;
  u32    uses;
};
struct Vmounts
{
  Vmount entries[VMOUNT_COUNT];
  u64    serial;
};

/* Serialized namespace, initially zero. The authorized caller keeps the
   root and mountpoint Vnode references until removal. A root with mount
   zero receives a new identity on publication. Only the initial root has
   no covering point. The caller supplies inode holds for these two nodes. */
isize vmountadd(Vmounts *mounts, Vnode *root, Vnode *point);
isize vmountdel(Vmounts *mounts, u64 id);
/* Count namespace use from node references, pending work or dirty state.
   These uses are independent of the holds owned by the mount table. */
isize vmounthold(Vmounts *mounts, u64 id);
isize vmountdrop(Vmounts *mounts, u64 id);
/* Borrow the visible root after lookup, or the node from which the backend
   should resolve '..'. The latter can return the caller's root directly;
   callers clamp there before dispatching a backend step. */
Vnode       *vmountenter(const Vmounts *mounts, Vnode *node);
const Vnode *vmountparent(const Vmounts *mounts, const Vnode *node, const Vnode *root);
#endif
