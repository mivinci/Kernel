#include "path.h"
#include <errno.h>
#include <string.h>

static isize
pathcheck(const char *path, u64 bytes)
{
  if (bytes > FS_PATH_BYTES)
    return -ENAMETOOLONG;
  if (!path || !bytes || memchr(path, 0, (size_t) bytes) != path + bytes - 1)
    return -EINVAL;
  return bytes == 1 ? -ENOENT : 0;
}

isize
fspathinit(Fspath *walk, const char *path, u64 bytes)
{
  isize error = pathcheck(path, bytes);
  if (!walk || error)
    return !walk ? -EINVAL : error;
  memmove(walk->text, path, (size_t) bytes);
  walk->bytes = (u32) bytes;
  walk->cursor = walk->links = walk->expandable = 0;
  return 0;
}

isize
fspathnext(Fspath *walk, Pathpart *part)
{
  Pathpart next = {0};
  u32      first, end, tail;
  if (!walk || !part || !walk->bytes || walk->cursor >= walk->bytes)
    return -EINVAL;
  first = walk->cursor;
  while (walk->text[first] == '/')
    first++;
  if (!walk->cursor && first) {
    next.kind = PATH_ROOT;
    next.final = !walk->text[first];
    next.directory = 1;
    end = first;
  } else {
    if (!walk->text[first]) {
      walk->expandable = 0;
      return 0;
    }
    end = first;
    while (walk->text[end] && walk->text[end] != '/')
      end++;
    next.name = walk->text + first;
    next.bytes = end - first;
    if (next.bytes > FS_NAME_BYTES)
      return -ENAMETOOLONG;
    tail = end;
    while (walk->text[tail] == '/')
      tail++;
    next.final = !walk->text[tail];
    next.directory = walk->text[end] == '/';
    next.kind = PATH_NAME;
    if (next.name[0] == '.') {
      if (next.bytes == 1)
        next.kind = PATH_DOT;
      else if (next.bytes == 2 && next.name[1] == '.')
        next.kind = PATH_PARENT;
    }
  }
  walk->cursor = end;
  walk->expandable = next.kind == PATH_NAME;
  *part = next;
  return 1;
}

isize
fspathexpand(Fspath *walk, const char *target, u64 bytes)
{
  u32   suffix, prefix;
  isize error;
  if (!walk || !walk->expandable)
    return -EINVAL;
  if (walk->links >= FS_PATH_LINKS)
    return -ELOOP;
  error = pathcheck(target, bytes);
  if (error)
    return error;
  prefix = (u32) bytes - 1;
  suffix = walk->bytes - walk->cursor;
  if (prefix > FS_PATH_BYTES - suffix)
    return -ENAMETOOLONG;
  memmove(walk->text + prefix, walk->text + walk->cursor, suffix);
  memcpy(walk->text, target, prefix);
  walk->bytes = prefix + suffix;
  walk->cursor = walk->expandable = 0;
  walk->links++;
  return 0;
}
