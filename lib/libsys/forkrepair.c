#include "private/fork.h"

isize
sysforkrepair(Forkdone *done, const Forkstate *state, Connpeer parent, const Forkret *result,
              u64 available, const Filefork *files, u64 file_count)
{
  Fixview         view;
  const Fixlayer *layers;
  const Fixconn  *connections;
  Connpeer        child;
  Handle          ignored;
  u64             count, mask, replaced;
  isize           status, restore;
  if (!done || !state || !result || result->construct || !result->fix)
    return -ERR_INVALID;
  status = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (status)
    return status;
  status = __sys_getid(&child.pid, &child.image, &ignored);
  if (!status && result->pid != child.pid)
    status = -ERR_STALE;
  if (!status && done->child.pid == child.pid && done->child.image == child.image) {
    if (done->fix != result->fix || done->parent.pid != parent.pid ||
        done->parent.image != parent.image)
      status = -ERR_STALE;
  } else if (!status) {
    status = sysfixchild(result, available, parent.pid, parent.image, &view);
    if (!status)
      status = sysfixlayers(&view, &layers);
    if (!status)
      status = sysfixconnections(&view, &connections, &count);
    if (!status)
      status =
          sysforkstateapply(state, parent, child, layers, files, file_count, connections, count);
    if (!status) {
      done->fix = result->fix;
      done->parent = parent;
      done->child = child;
    }
  }
  restore = __sys_sigmask(SIGNAL_SETMASK, mask, &replaced);
  return status ? status : restore;
}
