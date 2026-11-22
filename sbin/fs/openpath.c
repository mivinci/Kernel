#include "openpath.h"
#include <errno.h>

isize
vrequestpath(Vwalk *walk, const Vmounts *mounts, const Vwalkops *ops, void *context, Vnode *root,
             Vnode *start, u32 follow, Vcred *cred, const Filelink *link, const Ipcmsg *call,
             const Fileslot *slot)
{
  const volatile char *page;
  Vcred                effective;
  u64                  index, offset, bytes, state;
  u32                  i, chdir, readlink;
  isize                error;
  if (!link || !slot || !call || !cred || call->kind != IPC_CALL ||
      handlekind(call->request) != HANDLE_REQUEST)
    return -EINVAL;
  chdir = call->words[0] == FS_PATH && slot->args[0] == FS_PATH_CHDIR;
  readlink = call->words[0] == FS_PATH && slot->args[0] == FS_PATH_READLINK;
  if (!chdir && !readlink && call->words[0] != FS_OPEN)
    return -ENOSYS;
  index = call->words[3];
  if (!link->ready || !link->address || link->watch.owner.reason ||
      link->watch.connid != call->words[1] || link->watch.owner.pid != call->source ||
      link->watch.owner.image != call->image || index >= FS_CONN_SLOTS ||
      link->watch.active.requests[index] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ESTALE;
  error = ipc_rqcheck(call->request, &state);
  if (error || state == RQ_CHECK_DRAINING)
    return -ESTALE;
  if (state & RQ_CHECK_CANCEL)
    return -EINTR;
  if (state != RQ_CHECK_LIVE)
    return -EIO;
  if ((chdir || readlink) && slot->object)
    return -EINVAL;
  offset = slot->args[chdir || readlink ? 1 : 2];
  bytes = slot->args[chdir || readlink ? 2 : 3];
  if (slot->span != 1 || slot->payload_bytes > FS_SLOT_BYTES || offset > slot->payload_bytes ||
      !bytes || bytes > slot->payload_bytes - offset)
    return -EINVAL;
  if (readlink && (!slot->args[3] || slot->args[3] > FS_SLOT_BYTES))
    return -EINVAL;
  for (i = chdir ? 3 : 4; i < ARRAY_SIZE(slot->args); i++)
    if (slot->args[i])
      return -EINVAL;
  error = vcredsource(call, 0, &effective);
  if (error)
    return error;
  page = (const volatile char *) (link->address + FS_DATA_OFFSET + index * FS_SLOT_BYTES);
  error = vwalkinit(walk, mounts, ops, context, root, start, page + offset, bytes, follow);
  if (!error) {
    walk->searchlast = chdir;
    *cred = effective;
  }
  return error;
}
