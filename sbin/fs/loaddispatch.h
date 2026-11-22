#ifndef _FS_LOADDISPATCH_H
#define _FS_LOADDISPATCH_H
#include "loadio.h"
#include "private/filewatch.h"

typedef struct Loadjob Loadjob;
typedef struct Fsloads Fsloads;
struct Loadjob
{
  Loadio    io;
  Loadcache work;
  Filelink *link;
};
struct Fsloads
{
  Loadtable *table;
  Loadjob   *jobs;
  u32        count;
  u64        events;
  u8        *scratch; /* One CACHE_BYTES buffer shared by the serialized jobs. */
  void      *backend;
  /* Resolve a span from the decoded map of the admitted, pinned version.
     This callback starts no backing I/O. WOULD_BLOCK waits on the same
     LOAD; the backend must wake the loop when the snapshot can be read. */
  isize (*resolve)(void *, const Loadio *, Cache **, Loadspan *);
  /* On success the backend owns the read until it reports the exact
     completion to loadcachecomplete and wakes the service. On error it
     retains neither the ticket nor any outstanding access. WOULD_BLOCK
     keeps the job; the backend must wake the loop for retry. */
  isize (*submit)(void *, Loadjob *, const Loadfetch *);
};

isize fsloadstart(Fsloads *, Filelink *, u32, const Ipcmsg *);
isize fsloadpump(Fsloads *);
int   fsloadretire(Fsloads *, const Filelink *);
#endif
