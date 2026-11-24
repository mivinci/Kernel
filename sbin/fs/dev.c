#include "dev.h"
#include <errno.h>

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
