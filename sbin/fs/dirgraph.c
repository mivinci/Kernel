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

static isize
addname(Dirnames *names, const u8 *name, u32 length)
{
  u32 i;
  if (names->next || names->heap)
    return -ERR_STATE;
  if (names->count == names->slots || length + 1 > names->capacity - names->used)
    return -ERR_LIMIT;
  names->offsets[names->count++] = names->used;
  for (i = 0; i <= length; i++)
    names->bytes[names->used++] = name[i];
  return 0;
}

static int
namecmp(const Dirnames *names, u32 a, u32 b)
{
  const u8 *left = names->bytes + a, *right = names->bytes + b;
  while (*left && *left == *right) {
    left++;
    right++;
  }
  return (int) *left - (int) *right;
}

static void
namesift(Dirnames *names, u32 root)
{
  u32 *order = names->offsets;
  u32  item = order[root], child;
  while ((child = root * 2 + 1) < names->heap) {
    if (child + 1 < names->heap && namecmp(names, order[child], order[child + 1]) < 0)
      child++;
    if (namecmp(names, item, order[child]) >= 0)
      break;
    order[root] = order[child];
    root = child;
  }
  order[root] = item;
}

isize
dirnamesstep(Dirnames *names)
{
  u32 i;
  if (!names || !names->bytes || !names->offsets)
    return -ERR_INVALID;
  if (names->count < 2 || names->heap == 1)
    return 1;
  if (!names->heap) {
    names->heap = names->count;
    names->next = names->count / 2;
  }
  for (i = 0; i < 64 && names->heap > 1; i++) {
    u32 largest;
    if (names->next) {
      namesift(names, --names->next);
      continue;
    }
    largest = names->offsets[0];
    if (names->heap < names->count && !namecmp(names, largest, names->offsets[names->heap]))
      return -ERR_STATE;
    if (names->heap == 2 && !namecmp(names, names->offsets[1], largest))
      return -ERR_STATE;
    names->offsets[0] = names->offsets[--names->heap];
    names->offsets[names->heap] = largest;
    namesift(names, 0);
  }
  return names->heap == 1;
}

static isize
recordnext(const u8 *bytes, u32 valid, u32 *offset, Dirdecode decode, Dirrecord *record,
           const u8 **name)
{
  const u32 header = 16;
  u32       i, left = valid - *offset;
  if (left < header || decode(bytes + *offset, record) || record->reclen < header ||
      (record->reclen & 7) || record->reclen > left)
    return -ERR_STATE;
  *name = bytes + *offset + header;
  *offset += record->reclen;
  if (!record->inode)
    return 0;
  if (record->inode >= FS_INODES || !record->namelen || record->namelen > 255 ||
      record->namelen >= record->reclen - header || (*name)[record->namelen])
    return -ERR_STATE;
  for (i = 0; i < record->namelen; i++)
    if (!(*name)[i] || (*name)[i] == '/')
      return -ERR_STATE;
  return 1;
}

isize
dirblockfind(const Dirnode *nodes, const u8 *bytes, u32 valid, Dirdecode decode, const char *name,
             u32 namelen, Dirrecord *out)
{
  Dirrecord match;
  u32       offset = 0, found = 0, i;
  if (!nodes || !bytes || !valid || valid > FS_BLOCK_BYTES || !decode || !name || !namelen ||
      namelen > 255 || !out ||
      (name[0] == '.' && (namelen == 1 || (namelen == 2 && name[1] == '.'))))
    return -ERR_INVALID;
  for (i = 0; i < namelen; i++)
    if (!name[i] || name[i] == '/')
      return -ERR_INVALID;
  while (offset < valid) {
    Dirrecord      record = {0};
    const u8      *entry;
    const Dirnode *node;
    isize          result = recordnext(bytes, valid, &offset, decode, &record, &entry);
    if (result < 0)
      return result;
    if (!result)
      continue;
    node = &nodes[record.inode];
    if (!record.generation || record.generation != node->generation || !node->links ||
        record.kind != node->kind || record.kind < VNODE_FILE || record.kind > VNODE_LINK)
      return -ERR_STATE;
    if (record.namelen != namelen)
      continue;
    for (i = 0; i < namelen && entry[i] == (u8) name[i]; i++)
      ;
    if (i == namelen) {
      if (found)
        return -ERR_STATE;
      match = record;
      found = 1;
    }
  }
  if (found)
    *out = match;
  return found;
}

isize
dirgraphblock(Dirgraph *graph, Dirnames *names, u32 directory, const u8 *bytes, u32 valid,
              Dirdecode decode)
{
  const Dirnode *from;
  u32            offset = 0;
  if (!graph || !graph->nodes || !names || !names->bytes || !names->offsets || !bytes || !decode ||
      !directory || directory >= FS_INODES || !valid || valid > FS_BLOCK_BYTES)
    return -ERR_INVALID;
  if (graph->failed)
    return graph->failed;
  if (graph->next)
    return -ERR_STATE;
  from = &graph->nodes[directory];
  if (from->kind != VNODE_DIR || !from->links)
    return graph->failed = -ERR_STATE;
  while (offset < valid) {
    Dirrecord      record = {0};
    const u8      *name;
    const Dirnode *target;
    isize          error = recordnext(bytes, valid, &offset, decode, &record, &name);
    if (error < 0)
      return graph->failed = error;
    if (!error)
      continue;
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
    error = addname(names, name, record.namelen);
    if (!error)
      error = dirgraphedge(graph, directory, record.inode, record.generation, record.kind);
    if (error)
      return graph->failed = error;
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
