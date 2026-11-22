#include "walk.h"
#include <errno.h>

enum
{
  WALK_COMPONENT = 1,
  WALK_SEARCH,
  WALK_LOOKUP,
  WALK_LINK,
};

static int
same(const Vnode *left, const Vnode *right)
{
  return left->mount == right->mount && left->number == right->number &&
         left->generation == right->generation;
}

void
vwalkdrop(Vwalk *walk)
{
  if (walk->link)
    walk->ops->drop(walk->context, walk->link);
  if (walk->current)
    walk->ops->drop(walk->context, walk->current);
  if (walk->root)
    walk->ops->drop(walk->context, walk->root);
  walk->link = walk->current = walk->root = NULL;
  walk->phase = 0;
}

isize
vwalkinit(Vwalk *walk, const Vwalkops *ops, void *context, Vnode *root, Vnode *start,
          const char *path, u64 bytes, u32 follow)
{
  isize error;
  if (!walk || walk->phase || !ops || !ops->hold || !ops->drop || !ops->search || !ops->lookup ||
      !ops->parent || !ops->readlink || !root || root->kind != VNODE_DIR || follow > 1)
    return -EINVAL;
  error = fspathinit(&walk->path, path, bytes);
  if (error)
    return error;
  if (walk->path.text[0] == '/')
    start = root;
  if (!start || start->kind != VNODE_DIR)
    return -ENOTDIR;
  error = ops->hold(context, root);
  if (error)
    return error;
  error = ops->hold(context, start);
  if (error) {
    ops->drop(context, root);
    return error;
  }
  walk->ops = ops;
  walk->context = context;
  walk->root = root;
  walk->current = start;
  walk->follow = follow;
  walk->phase = WALK_COMPONENT;
  return 0;
}

static isize
component(Vwalk *walk, Vnode **out)
{
  isize status = fspathnext(&walk->path, &walk->part);
  if (status < 0)
    return status;
  if (!status) {
    *out = walk->current;
    walk->current = NULL;
    vwalkdrop(walk);
    return 0;
  }
  if (walk->part.kind == PATH_ROOT) {
    if (!same(walk->current, walk->root)) {
      status = walk->ops->hold(walk->context, walk->root);
      if (status)
        return status;
      walk->ops->drop(walk->context, walk->current);
      walk->current = walk->root;
    }
  } else {
    if (walk->current->kind != VNODE_DIR)
      return -ENOTDIR;
    walk->phase = WALK_SEARCH;
  }
  return VWALK_MORE;
}

static isize
lookup(Vwalk *walk)
{
  Vnode *node = NULL;
  isize  status;
  if (walk->part.kind == PATH_PARENT)
    status = walk->ops->parent(walk->context, walk->current, &node);
  else
    status =
        walk->ops->lookup(walk->context, walk->current, walk->part.name, walk->part.bytes, &node);
  if (status)
    return status;
  if (!node)
    return -EIO;
  if (node->kind == VNODE_LINK && walk->part.kind == PATH_NAME &&
      (!walk->part.final || walk->follow)) {
    walk->link = node;
    walk->phase = WALK_LINK;
    return VWALK_MORE;
  }
  if ((walk->part.directory || walk->part.kind == PATH_PARENT) && node->kind != VNODE_DIR) {
    walk->ops->drop(walk->context, node);
    return -ENOTDIR;
  }
  walk->ops->drop(walk->context, walk->current);
  walk->current = node;
  walk->phase = WALK_COMPONENT;
  return VWALK_MORE;
}

static isize
advance(Vwalk *walk, Vnode **out)
{
  const char *target;
  u64         bytes;
  isize       status;
  switch (walk->phase) {
  case WALK_COMPONENT:
    return component(walk, out);
  case WALK_SEARCH:
    status = walk->ops->search(walk->context, walk->current);
    if (status)
      return status;
    walk->phase = walk->part.kind == PATH_DOT ||
                          (walk->part.kind == PATH_PARENT && same(walk->current, walk->root))
                      ? WALK_COMPONENT
                      : WALK_LOOKUP;
    return VWALK_MORE;
  case WALK_LOOKUP:
    return lookup(walk);
  case WALK_LINK:
    status = walk->ops->readlink(walk->context, walk->link, &target, &bytes);
    if (!status)
      status = fspathexpand(&walk->path, target, bytes);
    if (status)
      return status;
    walk->ops->drop(walk->context, walk->link);
    walk->link = NULL;
    walk->phase = WALK_COMPONENT;
    return VWALK_MORE;
  default:
    return -EINVAL;
  }
}

isize
vwalkstep(Vwalk *walk, Vnode **out)
{
  isize status;
  if (!walk || !walk->phase || !out || *out)
    return -EINVAL;
  status = advance(walk, out);
  if (status < 0)
    vwalkdrop(walk);
  return status;
}
