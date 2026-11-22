#ifndef _FS_SERVE_H
#define _FS_SERVE_H
#include "pipedispatch.h"
#include "open.h"
#include "loaddispatch.h"
#include "journal.h"

typedef struct Fsservice Fsservice;
typedef struct Fsreclaim Fsreclaim;
struct Fsreclaim
{
  Reclaimtx *work;
  u64        events;
  void      *context;
  isize (*submit)(void *, const Journalio *, u64, u32);
  Reclaimscan *scan;
  isize (*candidate)(void *, u32 inode);
};
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
  /* Volume owner supplies a started batch and signals after lower drain. */
  Fsreclaim *reclaim;
};

/* The caller supplies actual authorities, an exclusive VA arena, and fixed
   zeroed storage. One serialized iteration; blocks when there is no work. */
isize fsstep(Fsservice *fs);
#endif
