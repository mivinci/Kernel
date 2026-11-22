#include "dirgraph.h"
#include "walk.h"

STATIC_ASSERT(FS_INODES - 1 <= UINT16_MAX, directory_parent_fits);

enum
{
  DirUnknown,
  DirVisiting,
  DirRooted
};

isize
dirgraphedge(Dirgraph *graph, u32 directory, u32 target, u32 generation, u32 kind)
{
  const Dirnode *from, *to;
  if (!graph || !graph->nodes)
    return -ERR_INVALID;
  if (graph->failed)
    return graph->failed;
  if (graph->next)
    return -ERR_STATE;
  if (!directory || directory >= FS_INODES || target <= FS_ROOT || target >= FS_INODES)
    return graph->failed = -ERR_STATE;
  from = &graph->nodes[directory];
  to = &graph->nodes[target];
  if (from->kind != VNODE_DIR || !from->links || !generation || generation != to->generation ||
      kind < VNODE_FILE || kind > VNODE_LINK || kind != to->kind || !to->links ||
      graph->incoming[target] >= to->links)
    return graph->failed = -ERR_STATE;
  if (kind == VNODE_DIR) {
    if (directory == target || graph->parent[target])
      return graph->failed = -ERR_STATE;
    graph->parent[target] = directory;
  }
  graph->incoming[target]++;
  return 0;
}

static isize
rooted(Dirgraph *graph, u32 ino)
{
  u32 current = ino;
  while (current && graph->reach[current] == DirUnknown) {
    graph->reach[current] = DirVisiting;
    current = graph->parent[current];
  }
  if (!current || graph->reach[current] != DirRooted)
    return -ERR_STATE;
  for (current = ino; graph->reach[current] == DirVisiting; current = graph->parent[current])
    graph->reach[current] = DirRooted;
  return 0;
}

isize
dirgraphblock(Dirgraph *graph, u32 directory, const u8 *bytes, u32 valid, Dirdecode decode)
{
  const Dirnode *from;
  u32            offset = 0;
  if (!graph || !graph->nodes || !bytes || !decode || !directory || directory >= FS_INODES ||
      !valid || valid > FS_BLOCK_BYTES)
    return -ERR_INVALID;
  if (graph->failed)
    return graph->failed;
  if (graph->next)
    return -ERR_STATE;
  from = &graph->nodes[directory];
  if (from->kind != VNODE_DIR || !from->links)
    return graph->failed = -ERR_STATE;
  while (offset < valid) {
    const u32      header = 16;
    Dirrecord      record = {0};
    const u8      *name;
    const Dirnode *target;
    u32            i;
    isize          error;
    if (valid - offset < header || decode(bytes + offset, &record) || record.reclen < header ||
        (record.reclen & 7) || record.reclen > valid - offset)
      return graph->failed = -ERR_STATE;
    name = bytes + offset + header;
    offset += record.reclen;
    if (!record.inode)
      continue;
    if (record.inode >= FS_INODES || !record.namelen || record.namelen > 255 ||
        record.namelen >= record.reclen - header || name[record.namelen])
      return graph->failed = -ERR_STATE;
    for (i = 0; i < record.namelen; i++)
      if (!name[i] || name[i] == '/')
        return graph->failed = -ERR_STATE;
    if (name[0] == '.' && (record.namelen == 1 || (record.namelen == 2 && name[1] == '.'))) {
      target = &graph->nodes[record.inode];
      if (record.kind != VNODE_DIR || target->kind != VNODE_DIR || !target->links ||
          !record.generation || target->generation != record.generation ||
          ((record.namelen == 1 || directory == FS_ROOT) && record.inode != directory))
        return graph->failed = -ERR_STATE;
      if (record.namelen == 1) {
        if (graph->dot[directory])
          return graph->failed = -ERR_STATE;
        graph->dot[directory] = 1;
      } else {
        if (graph->dotdot[directory])
          return graph->failed = -ERR_STATE;
        graph->dotdot[directory] = (u16) record.inode;
      }
      continue;
    }
    error = dirgraphedge(graph, directory, record.inode, record.generation, record.kind);
    if (error)
      return error;
  }
  return 0;
}

isize
dirgraphstep(Dirgraph *graph)
{
  u32 end;
  if (!graph || !graph->nodes)
    return -ERR_INVALID;
  if (graph->failed)
    return graph->failed;
  if (!graph->next) {
    const Dirnode *root = &graph->nodes[FS_ROOT];
    if (graph->nodes[0].kind || root->kind != VNODE_DIR || !root->links || !root->generation)
      return graph->failed = -ERR_STATE;
    graph->reach[FS_ROOT] = DirRooted;
    graph->next = FS_ROOT + 1;
  }
  end = MIN(graph->next + FS_BLOCK_BYTES / FS_INODE_BYTES, FS_INODES);
  while (graph->next < end) {
    u32            ino = graph->next++;
    const Dirnode *node = &graph->nodes[ino];
    if (!node->kind)
      continue;
    if (node->kind < VNODE_FILE || node->kind > VNODE_LINK || !node->generation ||
        (node->kind == VNODE_DIR
             ? (node->links && ((graph->dotdot[ino] && graph->dotdot[ino] != graph->parent[ino]) ||
                                rooted(graph, ino)))
             : node->links != graph->incoming[ino]))
      return graph->failed = -ERR_STATE;
  }
  return graph->next == FS_INODES;
}
