#include "private/elfloadfs.h"

static isize
abortread(Elfloadfs *source)
{
  u64   file;
  isize error = sysopabort(source->work, source->cache, &source->pending.key, &file);
  if (!error)
    source->pending.key.lease.seq = 0;
  return error;
}

static isize
restore(u64 mask, isize error)
{
  u64   ignored;
  isize status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : status;
}

isize
elfloadfsread(void *context, u64 offset, void *destination, u64 bytes)
{
  Elfloadfs *source = context;
  Ipcmsg     result;
  Msg        request;
  Handle     rpc;
  u64        mask, options = CALL_ASYNC;
  isize      error;
  if (!source || !destination)
    return -ERR_INVALID;
  if (source->pending.key.lease.seq || source->submitted)
    return -ERR_BUSY;
  source->business = 0;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysloadstage(source->work, source->cache, source->layer, source->owner, source->session,
                       source->identity, offset, bytes, &source->pending);
  if (error)
    return restore(mask, error);
  error = sysoprequest(source->work, source->cache, &source->pending.key, &request);
  if (source->pending.key.lease.slot >= 6)
    options |= CALL_CONTROL;
  if (!error)
    error = ipc_call(source->work->state.call, &request, options, &result);
  if (error) {
    isize cleanup = abortread(source);
    return restore(mask, cleanup ? cleanup : error);
  }
  rpc = source->submitted = result.request;
  error = sysopsubmitted(source->work, source->cache, &source->pending.key, &source->submitted);
  error = restore(mask, error);
  if (error)
    return error;
  do {
    error = ipc_rqwait(rpc, RQ_FINISH, &result);
  } while (error == -ERR_INTERRUPTED);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysloadcollect(source->work, source->cache, &source->pending, destination);
  if (!error) {
    source->business = source->pending.business;
    error = source->pending.native;
    if (!error && source->business)
      error = -ERR_STATE;
  }
  return restore(mask, error);
}

isize
elfloadfsdrain(Elfloadfs *source)
{
  u64   mask;
  isize error;
  if (!source)
    return -ERR_INVALID;
  if (!source->pending.key.lease.seq)
    return source->submitted ? -ERR_STATE : 0;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (source->submitted)
    error = sysopsubmitted(source->work, source->cache, &source->pending.key, &source->submitted);
  if (!error)
    error = sysopcheck(source->work, source->cache, &source->pending.key);
  if (!error &&
      source->work->state.operations[source->pending.key.lease.slot].phase == SYSOP_PREPARED)
    error = abortread(source);
  else if (!error)
    error = sysloadcollect(source->work, source->cache, &source->pending, NULL);
  return restore(mask, error);
}
