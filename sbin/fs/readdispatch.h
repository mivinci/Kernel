#ifndef _FS_READDISPATCH_H
#define _FS_READDISPATCH_H
#include "file.h"
#include "read.h"
#define FS_GROW_APPEND UINT64_MAX

typedef struct Fsreads Fsreads;
struct Readjob
{
  Readwork         work;
  Readjob         *prev, *next;
  Fsfile          *file;
  Filelink        *link;
  Ipcmsg           call;
  Fileslot         slot;
  const Loadmap   *map;
  const Loadindex *index;
  Cache           *cache;
  Loadtable       *loads;
  u32              first;
  u32              started, completed;
  isize            result;
};
struct Fsreads
{
  Readjob *jobs;
  u32      count;
  u64      events;
  void    *backend, *scratch;
  /* Supply a stable map and indirect snapshot through release. In-place
     writes also require the mount's LOAD exclusion table. */
  isize (*bind)(void *, Readjob *);
  void (*release)(void *, Readjob *);
  /* Copy the ticket, then finish via fsreadcomplete after transport drains
     and wake the loop. Failure means no external access was started. */
  isize (*submit)(void *, Readjob *, const Readfetch *);
  /* On the first call snapshot the input and take any needed inode guard.
     Subsequent calls get NULL input and the original request state. Return
     zero while pending, a positive committed byte count, or a negative
     business errno after drain. For FS_GROW_APPEND, choose EOF under the
     inode guard and report that starting offset with the committed result.
     A committed result survives cancellation. */
  isize (*grow)(void *, Readjob *, u64 state, u64 offset, const volatile u8 *input, u64 *start);
  /* Synchronize this File and its required metadata. The descriptor's
     first argument selects data-only sync. Start only on LIVE; resume with
     the actual request state until all lower accesses drain. Return zero
     while pending, one after durable success, or negative business errno.
     Backend completion is final even if the native reply must be retried. */
  isize (*sync)(void *, Readjob *, u64 state);
};

/* The caller has claimed the slot and checked original request state. */
isize fsdatastart(Fsreads *, Fsfiles *, Filelink *, const Ipcmsg *, const Fileslot *);
isize fssyncstart(Fsreads *, Fsfiles *, Filelink *, const Ipcmsg *, const Fileslot *);
isize fsseekstart(Fsreads *, Fsfiles *, Fsfile *, Filelink *, const Ipcmsg *, const Fileslot *);
isize fsreadpump(Fsreads *, Fsfiles *);
isize fsreadcomplete(Readjob *, Handle, const Cacheio *, const void *, u32);
int   fsreadbusy(const Fsreads *, const Filelink *);
#endif
