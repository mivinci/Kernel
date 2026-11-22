#ifndef _FS_PIPEDISPATCH_H
#define _FS_PIPEDISPATCH_H
#include "pipefile.h"
#include "pipeio.h"
#include "file.h"
#include "private/filewatch.h"

typedef struct Pipewait     Pipewait;
typedef struct Pipepoll     Pipepoll;
typedef struct Pipedispatch Pipedispatch;
struct Pipewait
{
  Pipeio    io;
  Pipefile *file;
  Filelink *link;
};
struct Pipepoll
{
  Handle    request;
  Filelink *link;
  u64       seq;
  u32       slot, count;
  Filepoll  rows[FS_POLL_ROWS];
};
struct Pipedispatch
{
  Pipetable *table;
  Pipewait  *waits;
  u32        count;
  Pipepoll  *polls;
  u32        npolls;
  Fsfiles   *regular;
};

/* Serialized FS loop; waits are preallocated and initially zero. Dispatch
   owns the received REQUEST, including on business error. A native error
   means service handling failed, not a result to send on a different Rq. */
isize pipedispatch(Pipedispatch *dispatch, Filelink *links, u32 count, const Ipcmsg *call);
/* Run after pipe transitions and on RQ_CANCEL/LIFE hints before sleeping. */
isize pipepump(Pipedispatch *dispatch);
/* Filelinksweep callback: retire exact-owner references and drain only this
   connection's old requests before its mapping can be released. */
isize pipedrain(void *context, Filelink *link);
#endif
