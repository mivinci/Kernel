#ifndef _FS_SERVE_H
#define _FS_SERVE_H
#include "pipedispatch.h"
#include "open.h"
#include "loaddispatch.h"

typedef struct Fsservice Fsservice;
struct Fsservice
{
  Handle       observe, allocator, space;
  u64          arena;
  Filelink    *links;
  u32          nlinks;
  Pipedispatch pipes;
  Fsopen      *open;
  u32          retiring;
  Fsloads     *loads;
};

/* The caller supplies actual authorities, an exclusive VA arena, and fixed
   zeroed storage. One serialized iteration; blocks when there is no work. */
isize fsstep(Fsservice *fs);
#endif
