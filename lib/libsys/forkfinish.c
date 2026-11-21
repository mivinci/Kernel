#include "private/fork.h"

isize
sysforkfinish(Elfwork *work, Rbank *bank, Connpeer parent, const Forkfix *fix)
{
  Fixlayer layers[FIX_LAYER_COUNT];
  Fixspec  spec;
  isize    status, cancel;
  if (!work || work->view <= 0 || work->pid <= 0 || work->mapped || work->memory || work->reserved)
    return -ERR_INVALID;
  if (work->failure)
    return -ERR_STATE;
  status = fix ? sysctxfixplan(bank, parent, layers) : -ERR_INVALID;
  if (!status) {
    spec.source = parent.pid;
    spec.image = parent.image;
    spec.target = work->pid;
    spec.layers = layers;
    spec.connections = fix->connections;
    spec.connection_count = fix->connection_count;
    spec.resources = fix->resources;
    spec.resource_count = fix->resource_count;
    status = sysfixpack(&spec, fix->buffer, fix->capacity);
    if (status > 0)
      return sysforkpublish(work, fix->address, fix->buffer, (u64) status);
  }
  work->failure = status;
  cancel = sysforkcancel(work);
  return cancel ? cancel : status;
}
