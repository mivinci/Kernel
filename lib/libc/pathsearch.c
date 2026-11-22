#include "private/pathsearch.h"
#include <errno.h>

int
pathsearch(const char *file, const char *path, char scratch[PATHSEARCH_BYTES],
           int (*attempt)(void *, const char *), void *context)
{
  u32 length, bytes, first, last, count = 1, i, used;
  int slash = 0, denied = 0, error;
  if (!file || !scratch || !attempt)
    return EINVAL;
  for (length = 0; length < PATHSEARCH_BYTES && file[length]; length++)
    if (file[length] == '/')
      slash = 1;
  if (length == PATHSEARCH_BYTES)
    return ENAMETOOLONG;
  if (!length)
    return ENOENT;
  if (slash)
    return attempt(context, file);
  if (!path)
    return EINVAL;
  for (bytes = 0; bytes < PATHSEARCH_BYTES && path[bytes]; bytes++)
    if (path[bytes] == ':')
      count++;
  if (bytes == PATHSEARCH_BYTES)
    return ENAMETOOLONG;
  if (count > PATHSEARCH_COUNT)
    return E2BIG;

  first = 0;
  for (;;) {
    last = first;
    while (last < bytes && path[last] != ':')
      last++;
    used = last - first;
    if (used && path[last - 1] != '/')
      used++;
    if (used > PATHSEARCH_BYTES - length - 1)
      return ENAMETOOLONG;
    for (i = 0; i < last - first; i++)
      scratch[i] = path[first + i];
    if (i < used)
      scratch[i] = '/';
    for (i = 0; i <= length; i++)
      scratch[used + i] = file[i];

    error = attempt(context, scratch);
    if (error == EACCES)
      denied = 1;
    else if (error != ENOENT && error != ENOTDIR)
      return error;
    if (last == bytes)
      return denied ? EACCES : ENOENT;
    first = last + 1;
  }
}
