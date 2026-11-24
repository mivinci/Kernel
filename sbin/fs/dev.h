#ifndef _FS_DEV_H
#define _FS_DEV_H
#include "file.h"

typedef struct Fsdev Fsdev;
struct Fsdev
{
  Vnode root, null;
  u32   published;
};

/* The caller installs this directory at /dev and owns its lifetime. */
isize fsdevpublish(Fsdev *, Fsfiles *);
/* Hide the node and reject pending opens. Existing Files keep their pin. */
isize fsdevwithdraw(Fsdev *, Fsfiles *);
isize fsdevlookup(Fsdev *, const Vnode *, const char *, u32, Vnode **);
#endif
