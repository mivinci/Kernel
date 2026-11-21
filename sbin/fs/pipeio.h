#ifndef _FS_PIPEIO_H
#define _FS_PIPEIO_H
#include "pipe.h"
#include "private/file.h"
#include <sys/channel.h>

typedef struct Pipeio Pipeio;
struct Pipeio
{
  Handle       request;
  u32          slot, end, bytes, nonblock, positional, fixed;
  volatile u8 *page;
  Msg          reply;
  u8           data[FS_SLOT_BYTES];
};

/* Dispatcher resolves and pins the authorized endpoint through completion.
   Start on an admitted REQUEST; if the first step blocks, defer the same
   REQUEST and call pipeiostep again after rechecking readiness. */
isize pipeiostart(Pipeio *io, const Fileinflight *active, const Ipcmsg *call, const Fileslot *slot,
                  volatile u8 *page, u32 end, u32 nonblock);
isize pipeiostep(Pipeio *io, Pipe *pipe, Fileinflight *active);
#endif
