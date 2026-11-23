#ifndef _FS_SERVE_H
#define _FS_SERVE_H
#include "pipedispatch.h"
#include "open.h"
#include "loaddispatch.h"
#include "journal.h"
#include "blk.h"
#include "blklog.h"
#include "private/execmanifest.h"

typedef struct Fsservice Fsservice;
typedef struct Fsreclaim Fsreclaim;
typedef struct Fsfork    Fsfork;
typedef struct Fsforks   Fsforks;
typedef union Forkhold   Forkhold;
union Forkhold
{
  Fsfile   *file;
  Pipefile *pipe;
};
struct Fsfork
{
  Execmanifest manifest;
  Forkhold     holds[BOOT_FD_COUNT];
  u64          context;
  u64          token; /* Nonzero after preparation, until child ATTACH. */
  u32          link;
  u32          execsource; /* One plus the source link index for EXEC_PREPARE. */
};
struct Fsforks
{
  Fsfork *records;
  u32     count;
  u64     serial;
};
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
  Blkfetch  *fetch;
  /* Optional volume journal bridge. The loop settles its ticket before
     file and reclaim workers try to submit more I/O. */
  Blklog  *log;
  u64      alarm;
  Fsforks *forks;
};

/* The caller supplies actual authorities, an exclusive VA arena, and fixed
   zeroed storage. The loop owns the process alarm and uses TIMER as a wakeup.
   One serialized iteration; blocks when there is no work. */
isize fsstep(Fsservice *fs);
#endif
