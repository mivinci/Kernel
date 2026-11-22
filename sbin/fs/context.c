#include "context.h"
#include "mount.h"
#include <errno.h>
#include <string.h>

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

void
fscwddrop(Fscwd *work)
{
  if (!work->root)
    return;
  work->ops->drop(work->backend, work->node);
  work->ops->drop(work->backend, work->start);
  work->ops->drop(work->backend, work->root);
  work->node = work->start = work->root = NULL;
}

static void
cwdreset(Fscwd *work)
{
  work->version = work->ops->version(work->backend);
  work->mounted = work->mounts->serial;
  work->removed = work->mounts->removed;
  work->first = sizeof(work->path) - 1;
  work->path[work->first] = 0;
}

isize
fscwdinit(Fscwd *work, const Vmounts *mounts, const Fscwdops *ops, void *backend,
          const Fscontext *context)
{
  isize error;
  if (!work || work->root || !mounts || !ops || !ops->hold || !ops->drop || !ops->version ||
      !ops->name || !context || !context->root || !context->cwd)
    return -EINVAL;
  error = ops->hold(backend, context->root);
  if (error)
    return error;
  error = ops->hold(backend, context->cwd);
  if (error) {
    ops->drop(backend, context->root);
    return error;
  }
  error = ops->hold(backend, context->cwd);
  if (error) {
    ops->drop(backend, context->cwd);
    ops->drop(backend, context->root);
    return error;
  }
  work->ops = ops;
  work->mounts = mounts;
  work->backend = backend;
  work->root = context->root;
  work->node = work->start = context->cwd;
  work->retries = work->waiting = 0;
  cwdreset(work);
  return 0;
}

static int
cwdchanged(Fscwd *work)
{
  return work->version != work->ops->version(work->backend) ||
         work->mounted != work->mounts->serial || work->removed != work->mounts->removed;
}

static isize
cwdretry(Fscwd *work)
{
  isize error;
  if (work->retries == 3)
    return -EAGAIN;
  error = work->ops->hold(work->backend, work->start);
  if (error)
    return error;
  work->ops->drop(work->backend, work->node);
  work->node = work->start;
  work->retries++;
  cwdreset(work);
  return VWALK_MORE;
}

static isize
cwdstep(Fscwd *work, char *out, u64 capacity, u32 *bytes)
{
  const Fscwdops *ops = work->ops;
  const Vnode    *origin;
  Vnode          *parent = NULL;
  const char     *name = NULL;
  u32             length = 0;
  isize           error;
  if (!work->waiting && cwdchanged(work))
    return cwdretry(work);
  if (vnodesame(work->node, work->root)) {
    if (work->first == sizeof(work->path) - 1)
      work->path[--work->first] = '/';
    length = sizeof(work->path) - work->first;
    if (length > capacity)
      return -ERANGE;
    memcpy(out, work->path + work->first, length);
    *bytes = length;
    return 0;
  }
  if (!work->waiting && !vnodesame(vmountenter(work->mounts, work->node), work->node))
    return -ENOENT;
  origin = vmountparent(work->mounts, work->node, work->root);
  /* Absolute lookup starts at the caller's root itself. It does not cross
     an overlay on that root unless it traverses a named mountpoint. */
  if (vnodesame(origin, work->root))
    return -ENOENT;
  error = ops->name(work->backend, origin, &parent, &name, &length);
  work->waiting = error == VWALK_WAIT;
  if (work->waiting)
    return VWALK_WAIT;
  if (cwdchanged(work)) {
    if (!error && parent)
      ops->drop(work->backend, parent);
    return cwdretry(work);
  }
  if (error)
    return error;
  if (!parent)
    return -EIO;
  if (!length || length > FS_NAME_BYTES || !name || parent->kind != VNODE_DIR ||
      parent->mount != origin->mount || vnodesame(parent, origin) || memchr(name, '/', length) ||
      memchr(name, 0, length) ||
      (name[0] == '.' && (length == 1 || (length == 2 && name[1] == '.'))))
    error = -EIO;
  else if (length >= work->first)
    error = -ENAMETOOLONG;
  if (error) {
    ops->drop(work->backend, parent);
    return error;
  }
  work->first -= length;
  memcpy(work->path + work->first, name, length);
  work->path[--work->first] = '/';
  ops->drop(work->backend, work->node);
  work->node = parent;
  return VWALK_MORE;
}

isize
fscwdstep(Fscwd *work, char *out, u64 capacity, u32 *bytes)
{
  isize status;
  if (!work || !work->root || !out || !bytes)
    return -EINVAL;
  status = cwdstep(work, out, capacity, bytes);
  if (status <= 0)
    fscwddrop(work);
  return status;
}
