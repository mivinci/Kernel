#include "mount.h"
#include <errno.h>

static u32
indexof(const Vmounts *mounts, u64 id)
{
  u32 i;
  for (i = 0; i < VMOUNT_COUNT; i++)
    if (mounts->entries[i].root && mounts->entries[i].root->mount == id)
      break;
  return i;
}

isize
vmountadd(Vmounts *mounts, Vnode *root, Vnode *point)
{
  u32 i, slot = VMOUNT_COUNT;
  if (!mounts || !root || root->mount || !root->number || !root->generation ||
      root->kind != VNODE_DIR || (!!point != !!mounts->serial))
    return -EINVAL;
  if (point && (point->kind != VNODE_DIR || !point->number || !point->generation ||
                indexof(mounts, point->mount) == VMOUNT_COUNT))
    return -EINVAL;
  for (i = 0; i < VMOUNT_COUNT; i++) {
    const Vmount *entry = &mounts->entries[i];
    if (!entry->root && slot == VMOUNT_COUNT)
      slot = i;
    if (point && entry->point && vnodesame(entry->point, point))
      return -EBUSY;
  }
  if (slot == VMOUNT_COUNT)
    return -ENOSPC;
  if (mounts->serial == UINT64_MAX)
    return -EOVERFLOW;
  root->mount = ++mounts->serial;
  mounts->entries[slot].root = root;
  mounts->entries[slot].point = point;
  return 0;
}

isize
vmountdel(Vmounts *mounts, u64 id)
{
  u32     i, index = indexof(mounts, id);
  Vmount *entry;
  if (index == VMOUNT_COUNT)
    return -ESTALE;
  entry = &mounts->entries[index];
  if (!entry->point || entry->uses)
    return -EBUSY;
  for (i = 0; i < VMOUNT_COUNT; i++)
    if (mounts->entries[i].point && mounts->entries[i].point->mount == id)
      return -EBUSY;
  entry->root = entry->point = NULL;
  mounts->removed++;
  return 0;
}

isize
vmounthold(Vmounts *mounts, u64 id)
{
  u32 index = indexof(mounts, id);
  if (index == VMOUNT_COUNT)
    return -ESTALE;
  if (mounts->entries[index].uses == UINT32_MAX)
    return -EOVERFLOW;
  mounts->entries[index].uses++;
  return 0;
}

isize
vmountdrop(Vmounts *mounts, u64 id)
{
  u32 index = indexof(mounts, id);
  if (index == VMOUNT_COUNT)
    return -ESTALE;
  if (!mounts->entries[index].uses)
    return -EINVAL;
  mounts->entries[index].uses--;
  return 0;
}

Vnode *
vmountenter(const Vmounts *mounts, Vnode *node)
{
  u32 i;
  /* Each child has a newer mount identity than its covering point. */
  for (;;) {
    for (i = 0; i < VMOUNT_COUNT; i++)
      if (mounts->entries[i].point && vnodesame(node, mounts->entries[i].point))
        break;
    if (i == VMOUNT_COUNT)
      return node;
    node = mounts->entries[i].root;
  }
}

const Vnode *
vmountparent(const Vmounts *mounts, const Vnode *node, const Vnode *root)
{
  while (!vnodesame(node, root)) {
    u32           index = indexof(mounts, node->mount);
    const Vmount *entry;
    if (index == VMOUNT_COUNT)
      break;
    entry = &mounts->entries[index];
    if (!entry->point || !vnodesame(entry->root, node))
      break;
    node = entry->point;
  }
  return node;
}
