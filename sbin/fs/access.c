#include "access.h"
#include "private/syscall.h"
#include <errno.h>

isize
vcredsource(const Ipcmsg *call, u32 real, Vcred *out)
{
  u32   ids[4];
  isize error;
  if (!call || call->kind != IPC_CALL || handlekind(call->request) != HANDLE_REQUEST || real > 1 ||
      !out)
    return -EINVAL;
  error = __sys_getcred(call->request, 0, 0, ids);
  if (error)
    return error == -ERR_STALE || error == -ERR_BAD_HANDLE ? -ESTALE : -EIO;
  out->uid = ids[real ? 0 : 1];
  out->gid = ids[real ? 2 : 3];
  return 0;
}

isize
vaccess(const Vcred *cred, u32 owner, u32 group, u32 mode, u32 requested, u32 directory)
{
  u32 allowed;
  if (!cred || requested & ~(VACCESS_R | VACCESS_W | VACCESS_X) || directory > 1)
    return -EINVAL;
  if (!cred->uid) {
    /* Root can search directories, but ordinary execute needs an execute bit. */
    return !(requested & VACCESS_X) || directory || (mode & 0111U) ? 0 : -EACCES;
  }
  allowed = mode >> (cred->uid == owner ? 6 : cred->gid == group ? 3 : 0);
  return (allowed & requested) == requested ? 0 : -EACCES;
}
