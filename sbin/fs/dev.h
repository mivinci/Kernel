#ifndef _FS_DEV_H
#define _FS_DEV_H
#include "walk.h"

typedef struct Fsdev Fsdev;
struct Fsdev
{
  Vnode root, null;
  u32   published;
};

/* The caller installs this directory at /dev and owns its lifetime. */
isize fsdevlookup(Fsdev *, const Vnode *, const char *, u32, Vnode **);
#endif
