#include "blklog.h"
#include <errno.h>

isize
blklogsubmit(void *context, const Journalio *ticket, u64 offset, u32 bytes)
{
  Blklog *log = context;
  u64     wantedoff;
  u32     wantedbytes;
  isize   error;
  if (!log || !log->blk || !log->journal)
    return -ERR_INVALID;
  if (log->ticket.serial || log->blk->rpc)
    return -ERR_WOULD_BLOCK;
  error = journalrange(log->journal, ticket, &wantedoff, &wantedbytes);
  if (error)
    return error;
  if (offset != wantedoff || bytes != wantedbytes)
    return -ERR_INVALID;
  error = fsblksubmit(log->blk, ticket->kind == JOURNAL_WRITE ? BLK_WRITE : BLK_FLUSH, offset,
                      bytes, ticket->bytes);
  if (!error)
    log->ticket = *ticket;
  return error;
}

isize
blklogpoll(Blklog *log)
{
  isize error;
  if (!log || !log->ticket.serial)
    return -ERR_STATE;
  if (!log->collected) {
    error = fsblkpoll(log->blk, NULL, 0, &log->status);
    if (error)
      return error;
    log->collected = 1;
  }
  error = log->status == -EAGAIN ? journalretry(log->journal, &log->ticket)
                                 : journalcomplete(log->journal, &log->ticket, !log->status);
  if (!error) {
    log->ticket.serial = 0;
    log->collected = 0;
  }
  return error;
}
