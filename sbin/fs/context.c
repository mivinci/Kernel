#include "context.h"
#include <errno.h>

static Fscontext *
find(const Fscontexts *table, Handle pid, u64 image)
{
  u32 i;
  if (!table || !table->entries || handlekind(pid) != HANDLE_PROCESS || !image)
    return NULL;
  for (i = 0; i < table->count; i++)
    if (table->entries[i].pid == pid && table->entries[i].image == image)
      return &table->entries[i];
  return NULL;
}

const Fscontext *
fscontextfind(const Fscontexts *table, Handle pid, u64 image)
{
  return find(table, pid, image);
}

isize
fscontextadd(Fscontexts *table, Handle pid, u64 image, Vnode *root, Vnode *cwd, u32 mask)
{
  u32   i, free;
  isize error;
  if (!table || !table->entries || !table->ops || !table->ops->hold || !table->ops->drop ||
      handlekind(pid) != HANDLE_PROCESS || !image || !root || !cwd || mask & ~0777U)
    return -EINVAL;
  if (root->kind != VNODE_DIR || cwd->kind != VNODE_DIR)
    return -ENOTDIR;
  free = table->count;
  for (i = 0; i < table->count; i++) {
    if (table->entries[i].pid == pid && table->entries[i].image == image)
      return -EEXIST;
    if (!table->entries[i].pid && free == table->count)
      free = i;
  }
  if (free == table->count)
    return -ENFILE;
  if (table->serial == UINT64_MAX)
    return -EOVERFLOW;
  error = table->ops->hold(table->backend, root);
  if (error)
    return error;
  error = table->ops->hold(table->backend, cwd);
  if (error) {
    table->ops->drop(table->backend, root);
    return error;
  }
  table->entries[free] = (Fscontext){pid, image, ++table->serial, root, cwd, mask};
  return 0;
}

isize
fscontextchdir(Fscontexts *table, Handle pid, u64 image, Vnode *cwd)
{
  Fscontext *entry = find(table, pid, image);
  Vnode     *old;
  isize      error;
  if (!entry)
    return -ESTALE;
  if (!cwd || cwd->kind != VNODE_DIR)
    return -ENOTDIR;
  if (table->serial == UINT64_MAX)
    return -EOVERFLOW;
  error = table->ops->hold(table->backend, cwd);
  if (error)
    return error;
  old = entry->cwd;
  entry->cwd = cwd;
  entry->version = ++table->serial;
  table->ops->drop(table->backend, old);
  return 0;
}

isize
fscontextremove(Fscontexts *table, Handle pid, u64 image)
{
  Fscontext *entry = find(table, pid, image);
  if (!entry)
    return -ESTALE;
  table->ops->drop(table->backend, entry->cwd);
  table->ops->drop(table->backend, entry->root);
  *entry = (Fscontext){0};
  return 0;
}
