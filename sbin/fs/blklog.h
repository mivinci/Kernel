#ifndef _FS_BLKLOG_H
#define _FS_BLKLOG_H
#include "blk.h"
#include "journal.h"

typedef struct Blklog Blklog;
struct Blklog
{
  Fsblk    *blk;
  Journal  *journal;
  Journalio ticket;
  isize     status;
  u32       collected;
};

/* Bind a zeroed bridge to its volume's block connection and journal.
   The serialized caller reserves the connection while a ticket is held. */
isize blklogsubmit(void *, const Journalio *, u64 offset, u32 bytes);
/* Consume at most one completion. Zero means the ticket was settled;
   a later journalpump may advance or retry once the device is ready. */
isize blklogpoll(Blklog *);
#endif
