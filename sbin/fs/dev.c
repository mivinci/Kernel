#include "dev.h"
#include <errno.h>

/* Only trusted FS startup code calls this after accepting init policy. */
isize
fsdevpublish(Fsdev *dev, Fsfiles *files)
{
  if (!dev || !files || dev->root.kind != VNODE_DIR || dev->null.kind != VNODE_CHAR ||
      !dev->root.generation || !dev->null.generation || dev->root.mount != dev->null.mount)
    return -EINVAL;
  if (dev->published || files->nullnode)
    return -EBUSY;
  files->nullnode = &dev->null;
  dev->published = 1;
  return 0;
}

isize
fsdevwithdraw(Fsdev *dev, Fsfiles *files)
{
  if (!dev || !files || !dev->published || files->nullnode != &dev->null)
    return -EINVAL;
  dev->published = 0;
  files->nullnode = NULL;
  return 0;
}

isize
fsdevlookup(Fsdev *dev, const Vnode *dir, const char *name, u32 bytes, Vnode **out)
{
  if (!dev || dir != &dev->root || !name || !out)
    return -EINVAL;
  if (!dev->published || bytes != 4 || name[0] != 'n' || name[1] != 'u' || name[2] != 'l' ||
      name[3] != 'l')
    return -ENOENT;
  *out = &dev->null;
  return 0;
}
