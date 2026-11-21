#include "seek.h"
#include <errno.h>

static isize
seekposition(u64 position, u64 length, const Fileslot *slot, u64 *out)
{
  u64     base, delta = slot->args[0];
  int64_t offset = (int64_t) delta;
  base = slot->args[1] == FS_SEEK_SET ? 0 : slot->args[1] == FS_SEEK_CUR ? position : length;
  if (offset < 0) {
    /* Compute the magnitude in unsigned arithmetic, including INT64_MIN. */
    delta = 0 - delta;
    if (delta > base)
      return -EINVAL;
    base -= delta;
  } else {
    if (base > (u64) INT64_MAX || delta > (u64) INT64_MAX - base)
      return -EOVERFLOW;
    base += delta;
  }
  if (base > (u64) INT64_MAX)
    return -EOVERFLOW;
  *out = base;
  return 0;
}

isize
fileseek(Fileinflight *active, const Ipcmsg *call, const Fileslot *slot, u64 length, u64 *position)
{
  u64   index, state, next = 0;
  isize error;
  Msg   reply = {{0, 0, 0, 0}};
  if (!active || !call || !slot || !position || call->kind != IPC_CALL ||
      call->words[0] != FS_SEEK || handlekind(call->request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  index = call->words[3];
  if (index >= FS_CONN_SLOTS || active->requests[index] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ERR_STALE;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
    return error ? error : sysfilefinish(active, call->request);
  }
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL))
    return -ERR_STATE;
  reply.words[1] = slot->seq;
  reply.words[0] =
      (u64) (state & RQ_CHECK_CANCEL      ? -EINTR
             : sysfileargs(FS_SEEK, slot) ? -EINVAL
                                          : seekposition(*position, length, slot, &next));
  reply.words[2] = next;
  error = ipc_reply(call->request, &reply);
  if (error)
    return error;
  /* No other operation on this File can run before the dispatcher resumes. */
  if (!reply.words[0])
    *position = next;
  return sysfilefinish(active, call->request);
}
