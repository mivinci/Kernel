#include "private/forkstate.h"
isize
sysforkstatecheck(const Forkstate *state, Connpeer parent, Connpeer child,
                  const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files, u64 file_count,
                  const Fixconn *connections, u64 connection_count)
{
  u64   i, j;
  isize error;
  if (!state || !state->fds || !state->files || !state->connections || !state->contexts)
    return -ERR_INVALID;
  error = sysctxfixwaitcheck(state->contexts, parent, child, layers);
  if (!error)
    error = sysconnforkcheck(state->connections, parent, connections, connection_count);
  if (!error)
    error = sysfilerefforkcheck(state->files, parent, child, files, file_count);
  if (!error)
    error = sysfdforkcheck(state->fds, files, file_count);
  if (error)
    return error;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    const Connentry *entry = &state->connections->entries[i];
    if (entry->generation != UINT64_MAX)
      continue;
    for (j = 0; j < connection_count; j++)
      if (connections[j].pid == entry->peer.pid && connections[j].image == entry->peer.image)
        return -ERR_OVERFLOW;
  }
  for (i = 0; i < RCTX_LAYERS; i++) {
    const Rroute *route = &state->contexts->contexts[i].route;
    if (route->kind == RCTX_ROUTE_OP && route->cache != state->connections)
      return -ERR_STALE;
  }
  for (i = 0; i < file_count; i++) {
    const Filefork  *plan = &files[i];
    const Fileref   *file = &state->files->entries[plan->file & 255];
    const Connentry *entry = &state->connections->entries[file->connection.index];
    if (entry->generation != file->connection.generation)
      return -ERR_STALE;
    for (j = 0; j < connection_count; j++)
      if (connections[j].pid == entry->peer.pid && connections[j].image == entry->peer.image)
        break;
    if (j == connection_count || connections[j].token != plan->value.token)
      return -ERR_STALE;
  }
  return 0;
}
