#ifndef _FS_READDISPATCH_H
#define _FS_READDISPATCH_H
#include "file.h"
#include "read.h"

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
  u32              first;
};
struct Fsreads
{
  Readjob *jobs;
  u32      count;
  u64      events;
  void    *backend, *scratch;
  /* Supply a stable map and indirect snapshot through release. */
  isize (*bind)(void *, Readjob *);
  void (*release)(void *, Readjob *);
  /* Copy the ticket, then finish via fsreadcomplete after transport drains
     and wake the loop. Failure means no external access was started. */
  isize (*submit)(void *, Readjob *, const Readfetch *);
};

/* The caller has claimed the slot and checked original request state. */
isize fsreadstart(Fsreads *, Fsfiles *, Filelink *, const Ipcmsg *, const Fileslot *);
isize fsseekstart(Fsreads *, Fsfiles *, Fsfile *, Filelink *, const Ipcmsg *, const Fileslot *);
isize fsreadpump(Fsreads *, Fsfiles *);
isize fsreadcomplete(Readjob *, Handle, const Cacheio *, const void *, u32);
int   fsreadbusy(const Fsreads *, const Filelink *);
#endif
