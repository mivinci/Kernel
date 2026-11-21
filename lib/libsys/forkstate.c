#include "private/forkstate.h"
typedef struct Region Region;
struct Region
{
  const void *address;
  u64         bytes;
};
static int
separate(Region a, Region b)
{
  u64 x = (u64) (uintptr_t) a.address, y = (u64) (uintptr_t) b.address;
  return !a.bytes || !b.bytes ||
         (a.address && b.address && a.bytes <= UINT64_MAX - x && b.bytes <= UINT64_MAX - y &&
          (x + a.bytes <= y || y + b.bytes <= x));
}
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
isize
sysforkstateapply(const Forkstate *state, Connpeer parent, Connpeer child,
                  const Fixlayer layers[FIX_LAYER_COUNT], const Filefork *files, u64 file_count,
                  const Fixconn *connections, u64 connection_count)
{
  Region regions[8], transport;
  u32    i, j;
  isize  error;
  if (!state || file_count > SYSFILE_REFS || connection_count > FIX_CONN_COUNT)
    return -ERR_INVALID;
  regions[0].address = state;
  regions[0].bytes = sizeof(*state);
  regions[1].address = state->fds;
  regions[1].bytes = sizeof(*state->fds);
  regions[2].address = state->files;
  regions[2].bytes = sizeof(*state->files);
  regions[3].address = state->connections;
  regions[3].bytes = sizeof(*state->connections);
  regions[4].address = state->contexts;
  regions[4].bytes = sizeof(*state->contexts);
  regions[5].address = layers;
  regions[5].bytes = sizeof(*layers) * FIX_LAYER_COUNT;
  regions[6].address = files;
  regions[6].bytes = sizeof(*files) * file_count;
  regions[7].address = connections;
  regions[7].bytes = sizeof(*connections) * connection_count;
  for (i = 0; i < ARRAY_SIZE(regions); i++)
    for (j = 0; j < i; j++)
      if (!separate(regions[i], regions[j]))
        return -ERR_INVALID;
  error = sysforkstatecheck(state, parent, child, layers, files, file_count, connections,
                            connection_count);
  if (error)
    return error;
  for (i = 0; i < BOOT_FD_COUNT; i++)
    if (state->fds->entries[i].state == SYSFD_RESERVED)
      return -ERR_NOTSUP;
  for (i = 0; i < SYSFILE_REFS; i++)
    if (state->files->entries[i].state == SYSFILE_REF_RESERVED)
      return -ERR_NOTSUP;
  for (i = 0; i < RCTX_LAYERS; i++) {
    const Rroute *route = &state->contexts->contexts[i].route;
    transport.address =
        route->kind == RCTX_ROUTE_BOOT ? (void *) route->bootstrap : (void *) route->work;
    transport.bytes = route->kind == RCTX_ROUTE_BOOT ? sizeof(Bootstrap)
                      : route->kind == RCTX_ROUTE_OP ? sizeof(Connstate)
                                                     : 0;
    for (j = 1; j <= 4; j++)
      if (!separate(transport, regions[j]))
        return -ERR_INVALID;
  }
  /* Every fallible admission precedes writes. These three local operations
     recheck the same stable, disjoint inputs and cannot wait or invoke IPC. */
  error = sysctxfixwait(state->contexts, parent, child, layers);
  if (!error)
    error = sysfilereffork(state->files, parent, child, files, file_count);
  if (!error)
    error = sysconnfork(state->connections, parent, child, connections, connection_count);
  return error;
}
