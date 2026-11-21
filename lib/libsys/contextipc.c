#include "private/contextipc.h"
#include "private/filecreate.h"
#include <abi/syscall.h>
#include <proto/file.h>
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static void
clearroute(Rroute *route)
{
  u32          i;
  volatile u8 *bytes = (volatile u8 *) route;
  for (i = 0; i < sizeof(*route); i++)
    bytes[i] = 0;
}
static isize
forkfixed(Rctx *context)
{
  Rroute expected;
  u32    i;
  if ((context->phase != RCTX_DONE && context->phase != RCTX_CLEANING) ||
      context->status != -ERR_INTERRUPTED || context->number || context->route.delivered > 1)
    return -ERR_STATE;
  clearroute(&expected);
  expected.kind = RCTX_ROUTE_FORK;
  expected.delivered = context->route.delivered;
  for (i = 0; i < sizeof(expected); i++)
    if (((const u8 *) &expected)[i] != ((const u8 *) &context->route)[i])
      return -ERR_STATE;
  for (i = 0; i < ARRAY_SIZE(context->args); i++)
    if (context->args[i])
      return -ERR_STATE;
  for (i = 0; i < sizeof(context->result); i++)
    if (((const u8 *) &context->result)[i])
      return -ERR_STATE;
  return 0;
}
static isize
forkwait(Rctx *context, Connpeer parent)
{
  Rroute *route = &context->route;
  Handle  rpc;
  isize   error;
  u32     i;
  if (context->phase != RCTX_WAITING || context->number != SYS_RQWAIT || route->delivered ||
      route->snapshot_saved || route->snapshot_bytes || route->snapshot_offset)
    return -ERR_NOTSUP;
  for (i = 1; i < ARRAY_SIZE(context->args); i++)
    if (context->args[i])
      return -ERR_STATE;
  if (route->kind == RCTX_ROUTE_BOOT) {
    if (!route->bootstrap)
      return -ERR_STATE;
    error = sysbootrpc(route->bootstrap, route->claim, &rpc);
  } else if (route->kind == RCTX_ROUTE_OP) {
    if (!route->work || !route->cache || route->work->state.self.pid != parent.pid ||
        route->work->state.self.image != parent.image || route->cache->self.pid != parent.pid ||
        route->cache->self.image != parent.image)
      return -ERR_STALE;
    error = sysoprpc(route->work, route->cache, &route->operation, &rpc);
  } else
    return -ERR_NOTSUP;
  if (error)
    return error;
  return rpc == route->rpc && (u64) rpc == context->args[0] ? 0 : -ERR_STALE;
}
isize
sysctxfixwait(Rbank *bank, Connpeer parent, Connpeer child, const Fixlayer layers[FIX_LAYER_COUNT])
{
  u32   i, j;
  isize error;
  if (!bank || !layers || handlekind(child.pid) != HANDLE_PROCESS || !child.image ||
      handlekind(parent.pid) != HANDLE_PROCESS || !parent.image || child.pid == parent.pid ||
      !separate(bank, sizeof(*bank), layers, sizeof(*layers) * FIX_LAYER_COUNT))
    return -ERR_INVALID;
  if (bank->self.pid == child.pid && bank->self.image == child.image)
    return 0;
  error = sysctxfixcheck(bank, parent, layers);
  if (error)
    return error;
  for (i = 0; i < RCTX_LAYERS; i++) {
    if (layers[i].action == FIX_LAYER_EMPTY)
      continue;
    if (bank->contexts[i].route.kind == RCTX_ROUTE_NONE) {
      error = sysctxfixrawlayer(&bank->contexts[i], &layers[i]);
      if (error)
        return error;
      continue;
    }
    if (bank->contexts[i].route.kind == RCTX_ROUTE_FORK) {
      error = forkfixed(&bank->contexts[i]);
      if (error)
        return error;
      continue;
    }
    if (layers[i].action != FIX_LAYER_INTERRUPT)
      return -ERR_NOTSUP;
    error = forkwait(&bank->contexts[i], parent);
    if (error)
      return error;
  }
  for (i = 0; i < RCTX_LAYERS; i++) {
    Rctx *context = &bank->contexts[i];
    if (layers[i].action != FIX_LAYER_INTERRUPT || context->route.kind == RCTX_ROUTE_FORK)
      continue;
    if (context->route.kind != RCTX_ROUTE_NONE) {
      clearroute(&context->route);
      context->route.kind = RCTX_ROUTE_FORK;
    }
    context->status = -ERR_INTERRUPTED;
    context->number = 0;
    for (j = 0; j < ARRAY_SIZE(context->args); j++)
      context->args[j] = 0;
    for (j = 0; j < sizeof(context->result); j++)
      ((u8 *) &context->result)[j] = 0;
    context->phase = RCTX_DONE;
  }
  bank->self = child;
  return 0;
}
isize
sysctxforkresult(Rbank *bank, Connpeer self, Rkey key, isize *status)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), status, sizeof(*status)))
    return -ERR_INVALID;
  if (context->route.kind != RCTX_ROUTE_FORK)
    return 0;
  error = forkfixed(context);
  if (error)
    return error;
  *status = context->status;
  context->route.delivered = 1;
  context->phase = RCTX_CLEANING;
  return 1;
}
static int
storage(Rbank *bank, const void *record, u64 bytes, Rkey *out)
{
  return separate(bank, sizeof(*bank), record, bytes) && separate(record, bytes, out, sizeof(*out));
}
static int
empty(const Ipcmsg *message, u32 request)
{
  u32 i;
  if (message->kind || message->source || message->image || (!request && message->request))
    return 0;
  for (i = 0; i < 4; i++)
    if (message->words[i])
      return 0;
  return 1;
}
static isize
prepare(Rbank *bank, Connpeer self, u32 layer, u64 number, Handle target, const Msg *message,
        u64 options, const Rroute *route, Rkey *out)
{
  u64   args[7];
  u32   i;
  isize error;
  args[0] = (u64) target;
  for (i = 1; i < 7; i++)
    args[i] = 0;
  if (number == SYS_CALL) {
    if (handlekind(target) != HANDLE_RESOURCE ||
        options & ~((u64) (CALL_RECV_MEM | CALL_RECV_CALL | CALL_CONTROL)))
      return -ERR_INVALID;
    for (i = 0; i < 4; i++)
      args[1 + i] = message->words[i];
    args[5] = CALL_ASYNC | options;
  }
  error = sysctxprepare(bank, self, layer, number, args, out);
  if (error)
    return error;
  for (i = 0; i < sizeof(*route); i++)
    ((u8 *) &bank->contexts[layer].route)[i] = ((const u8 *) route)[i];
  return 0;
}
isize
sysctxbootcall(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim,
               Handle call, u64 options, Rkey *out)
{
  Rroute route;
  Msg    request;
  isize  error;
  if (!storage(bank, record, sizeof(*record), out) || options & CALL_CONTROL)
    return -ERR_INVALID;
  clearroute(&route);
  error = sysbootrequest(record, claim, &request);
  if (error)
    return error;
  if (record->opcode == FS_ABORT) {
    if (options)
      return -ERR_INVALID;
    options = CALL_CONTROL;
  }
  error = sysconnpeer(call, &route.target);
  if (error)
    return error;
  route.kind = RCTX_ROUTE_BOOT;
  route.bootstrap = record;
  route.claim = claim;
  return prepare(bank, self, layer, SYS_CALL, call, &request, options, &route, out);
}
isize
sysctxbootwait(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim, Rkey *out)
{
  Rroute route;
  isize  error;
  if (!storage(bank, record, sizeof(*record), out))
    return -ERR_INVALID;
  clearroute(&route);
  error = sysbootrpc(record, claim, &route.rpc);
  if (error)
    return error;
  route.kind = RCTX_ROUTE_BOOT;
  route.bootstrap = record;
  route.claim = claim;
  return prepare(bank, self, layer, SYS_RQWAIT, route.rpc, NULL, 0, &route, out);
}
static isize
operation(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
          const Opkey *key, u64 options, u64 number, Rkey *out)
{
  Rroute route;
  Msg    request;
  Handle target;
  isize  error;
  if (!storage(bank, work, sizeof(*work), out) || !storage(bank, cache, sizeof(*cache), out) ||
      !separate(key, sizeof(*key), out, sizeof(*out)) || options & CALL_CONTROL)
    return -ERR_INVALID;
  if (work->state.self.pid != self.pid || work->state.self.image != self.image)
    return -ERR_STALE;
  clearroute(&route);
  if (number == SYS_CALL) {
    error = sysoprequest(work, cache, key, &request);
    target = work->state.call;
    if (!error && key->lease.slot >= 6)
      options |= CALL_CONTROL;
  } else {
    error = sysoprpc(work, cache, key, &route.rpc);
    target = route.rpc;
  }
  if (error)
    return error;
  if (pool) {
    if (!separate(bank, sizeof(*bank), pool, sizeof(*pool)) ||
        !separate(pool, sizeof(*pool), out, sizeof(*out)))
      return -ERR_INVALID;
    error = sysfilecreatecheck(pool, work, cache, key);
    if (error)
      return error;
    if (number == SYS_CALL && work->state.operations[key->lease.slot].opcode == FS_OPEN)
      options |= CALL_RECV_CALL;
    route.pool = pool;
  } else if (work->state.operations[key->lease.slot].flags & SYSOP_CREATOR)
    return -ERR_STATE;
  route.kind = RCTX_ROUTE_OP;
  route.work = work;
  route.cache = cache;
  route.operation.connection.generation = key->connection.generation;
  route.operation.connection.index = key->connection.index;
  route.operation.connection.reserved = key->connection.reserved;
  route.operation.lease.seq = key->lease.seq;
  route.operation.lease.slot = key->lease.slot;
  route.operation.lease.span = key->lease.span;
  route.operation.owner = key->owner;
  return prepare(bank, self, layer, number, target, &request, options, &route, out);
}
isize
sysctxopcall(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
             const Opkey *key, u64 options, Rkey *out)
{
  return operation(bank, self, layer, NULL, work, cache, key, options, SYS_CALL, out);
}
isize
sysctxopwait(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
             const Opkey *key, Rkey *out)
{
  return operation(bank, self, layer, NULL, work, cache, key, 0, SYS_RQWAIT, out);
}
isize
sysctxcreatecall(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                 Conncache *cache, const Opkey *key, Rkey *out)
{
  if (!pool)
    return -ERR_INVALID;
  return operation(bank, self, layer, pool, work, cache, key, 0, SYS_CALL, out);
}
isize
sysctxcreatewait(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                 Conncache *cache, const Opkey *key, Rkey *out)
{
  if (!pool)
    return -ERR_INVALID;
  return operation(bank, self, layer, pool, work, cache, key, 0, SYS_RQWAIT, out);
}
isize
sysctxcreateresult(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx   *context;
  Rroute *route;
  isize   error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  route = &context->route;
  if (context->phase != RCTX_CLEANING || route->kind != RCTX_ROUTE_OP || !route->pool ||
      route->delivered != 1)
    return -ERR_STATE;
  error = sysfilecreateresult(route->pool, route->work, route->cache, &route->operation);
  route->capture_status = error;
  return error;
}
isize
sysctxpeerended(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx   *context;
  Rroute *route;
  isize   error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  route = &context->route;
  if (context->phase != RCTX_CLEANING || context->number != SYS_CALL ||
      context->status != -ERR_PEER || route->kind != RCTX_ROUTE_OP || route->delivered != 1)
    return -ERR_STATE;
  error = sysopcheck(route->work, route->cache, &route->operation);
  if (error)
    return error;
  if (context->args[0] != (u64) route->work->state.call ||
      route->work->state.operations[route->operation.lease.slot].phase != SYSOP_PREPARED)
    return -ERR_STALE;
  error = sysconninvalidate(route->cache, route->operation.connection);
  if (!error)
    route->work->state.ended = 1;
  return error;
}
isize
sysctxbootpeerended(Rbank *bank, Connpeer self, Rkey key, Connwork *work, Conncache *cache)
{
  Rctx      *context;
  Rroute    *route;
  Bootstrap *boot;
  Connentry  entry;
  Connpeer   peer;
  u32        i;
  isize      error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), work, sizeof(*work)) ||
      !separate(bank, sizeof(*bank), cache, sizeof(*cache)) ||
      !separate(work, sizeof(work->state), cache, sizeof(*cache)))
    return -ERR_INVALID;
  if (work->state.self.pid != self.pid || work->state.self.image != self.image)
    return -ERR_STALE;
  error = sysconncheck(cache, self);
  if (!error)
    error = sysconnview(cache, work->state.tag, &entry);
  if (error)
    return error;
  if (work->state.ended > 1 || (work->state.ended && entry.state != SYSCONN_DRAINING))
    return -ERR_STATE;
  route = &context->route;
  boot = &work->state.bootstrap;
  if (context->phase != RCTX_CLEANING || context->status != -ERR_PEER ||
      route->kind != RCTX_ROUTE_BOOT || route->delivered != 1 || route->bootstrap != boot ||
      sysbootcheck(boot) ||
      (boot->opcode != FS_HELLO && boot->opcode != FS_ATTACH && boot->opcode != FS_EXEC_CLAIM))
    return -ERR_STATE;
  if (!route->target.pid || !route->target.image || route->target.pid != work->state.peer.pid ||
      route->target.image != work->state.peer.image || entry.peer.pid != route->target.pid ||
      entry.peer.image != route->target.image || route->claim.tag.reserved ||
      route->claim.tag.generation != work->state.tag.generation ||
      route->claim.tag.index != work->state.tag.index || route->claim.seq != entry.sequence ||
      route->claim.owner != entry.owner ||
      route->claim.tag.generation != boot->claim.tag.generation ||
      route->claim.tag.index != boot->claim.tag.index || route->claim.seq != boot->claim.seq ||
      route->claim.owner != boot->claim.owner)
    return -ERR_STALE;
  if (context->number == SYS_CALL) {
    if (boot->state != SYSBOOT_PREPARED || context->args[1] != boot->opcode || context->args[2] ||
        context->args[3] != boot->claim.seq || context->args[4] != boot->token ||
        !(context->args[5] & CALL_ASYNC) ||
        context->args[5] & ~((u64) (CALL_ASYNC | CALL_RECV_MEM | CALL_RECV_CALL)) ||
        context->args[6])
      return -ERR_STATE;
    error = sysconnpeer((Handle) context->args[0], &peer);
    if (error)
      return error;
    if (peer.pid != route->target.pid || peer.image != route->target.image)
      return -ERR_DENIED;
  } else if (context->number == SYS_RQWAIT) {
    if (boot->state != SYSBOOT_RESULT || boot->status != -ERR_PEER || boot->kind != IPC_NONE ||
        boot->flags || boot->resource || boot->rpc != route->rpc ||
        context->args[0] != (u64) route->rpc)
      return -ERR_STATE;
    for (i = 1; i < 7; i++)
      if (context->args[i])
        return -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (boot->words[i])
        return -ERR_STATE;
  } else
    return -ERR_STATE;
  error = sysconninvalidate(cache, work->state.tag);
  if (!error)
    work->state.ended = 1;
  return error;
}
isize
sysctxcreatepeerended(Rbank *bank, Connpeer self, Rkey key, Filepool *pool, Connwork *work,
                      Conncache *cache, const Opkey *operation)
{
  Rctx      *context;
  Rroute    *route;
  Bootstrap *boot;
  Createop  *create;
  Slotop    *op;
  Connpeer   peer;
  isize      error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), pool, sizeof(*pool)) ||
      !separate(bank, sizeof(*bank), work, sizeof(*work)) ||
      !separate(bank, sizeof(*bank), cache, sizeof(*cache)))
    return -ERR_INVALID;
  error = sysfilecreatecheck(pool, work, cache, operation);
  if (error)
    return error;
  if (work->state.self.pid != self.pid || work->state.self.image != self.image)
    return -ERR_STALE;
  route = &context->route;
  boot = &work->state.bootstrap;
  create = &work->state.creators[operation->lease.slot];
  op = &work->state.operations[operation->lease.slot];
  if (context->phase != RCTX_CLEANING || context->status != -ERR_PEER ||
      route->kind != RCTX_ROUTE_BOOT || route->delivered != 1 || route->bootstrap != boot ||
      sysbootcheck(boot) || boot->opcode != FS_ABORT || boot->token != create->token ||
      boot->claim.tag.generation || boot->claim.tag.index != SYSCONN_COUNT ||
      boot->claim.owner != create->files[0] ||
      route->claim.tag.generation != boot->claim.tag.generation ||
      route->claim.tag.index != boot->claim.tag.index || route->claim.seq != boot->claim.seq ||
      route->claim.tag.reserved || route->claim.owner != boot->claim.owner ||
      create->phase != SYSFILE_CREATE_CAPTURED || create->count != 1 || create->error ||
      !create->token || op->phase != SYSOP_ACKED || op->opcode != FS_OPEN ||
      op->kind != IPC_RESOURCE_REPLY || route->target.pid != create->peer.pid ||
      route->target.image != create->peer.image)
    return -ERR_STATE;
  if (context->number == SYS_CALL) {
    if (boot->state != SYSBOOT_PREPARED || context->args[1] != FS_ABORT || context->args[2] ||
        context->args[3] != boot->claim.seq || context->args[4] != create->token ||
        context->args[5] != (CALL_ASYNC | CALL_CONTROL) || context->args[6])
      return -ERR_STATE;
    error = sysconnpeer((Handle) context->args[0], &peer);
    if (error)
      return error;
    if (peer.pid != create->peer.pid || peer.image != create->peer.image)
      return -ERR_DENIED;
  } else if (context->number == SYS_RQWAIT) {
    u32 i;
    if (boot->state != SYSBOOT_RESULT || boot->status != -ERR_PEER || boot->kind != IPC_NONE ||
        boot->flags || boot->resource || boot->rpc != route->rpc ||
        context->args[0] != (u64) route->rpc)
      return -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (boot->words[i])
        return -ERR_STATE;
  } else
    return -ERR_STATE;
  create->flags |= SYSFILE_CREATE_PEER_ENDED;
  return 0;
}
isize
sysctxhandoff(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx   *context;
  Rroute *route;
  Ipcmsg  message;
  Handle  rpc;
  u32     i;
  isize   error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  route = &context->route;
  if (route->kind != RCTX_ROUTE_BOOT && route->kind != RCTX_ROUTE_OP)
    return -ERR_STATE;
  if (context->phase == RCTX_CLEANING && route->delivered == 1)
    return 0;
  if (context->phase != RCTX_DONE || route->delivered || context->status > 0)
    return -ERR_STATE;
  for (i = 0; i < sizeof(message); i++)
    ((u8 *) &message)[i] = ((const u8 *) &context->result)[i];
  if (context->number == SYS_CALL) {
    Msg request;
    error = route->kind == RCTX_ROUTE_BOOT
                ? sysbootrequest(route->bootstrap, route->claim, &request)
                : sysoprequest(route->work, route->cache, &route->operation, &request);
    if (error)
      return error;
    if (!empty(&message, !context->status))
      return -ERR_STATE;
    if (!context->status) {
      if (handlekind(message.request) != HANDLE_RPC)
        return -ERR_STATE;
      error = route->kind == RCTX_ROUTE_BOOT
                  ? sysbootsubmitted(route->bootstrap, route->claim, &message.request)
                  : sysopsubmitted(route->work, route->cache, &route->operation, &message.request);
      if (error)
        return error;
    }
  } else if (context->number == SYS_RQWAIT) {
    error = route->kind == RCTX_ROUTE_BOOT
                ? sysbootrpc(route->bootstrap, route->claim, &rpc)
                : sysoprpc(route->work, route->cache, &route->operation, &rpc);
    if (error)
      return error;
    if (rpc != route->rpc || (Handle) context->args[0] != rpc)
      return -ERR_STALE;
    if (!context->status || context->status == -ERR_CANCELLED || context->status == -ERR_PEER ||
        context->status == -ERR_REVOKED) {
      error = route->kind == RCTX_ROUTE_BOOT
                  ? sysbootcapture(route->bootstrap, route->claim, context->status, &message)
                  : sysopcapture(route->work, route->cache, &route->operation, context->status,
                                 &message);
      if (error)
        return error;
    } else if (!empty(&message, 0))
      return -ERR_STATE;
  } else
    return -ERR_STATE;
  /* The destination now owns RPC/result/resource, or retains the original
     waiting responsibility after an error. Keep only status for resume. */
  for (i = 0; i < sizeof(context->result); i++)
    ((u8 *) &context->result)[i] = 0;
  route->delivered = 1;
  context->phase = RCTX_CLEANING;
  return 0;
}
isize
sysctxrewait(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx  *context;
  Handle rpc;
  u32    i;
  isize  error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_CLEANING || context->route.delivered != 1 ||
      context->route.snapshot_saved)
    return -ERR_STATE;
  if (context->route.kind == RCTX_ROUTE_BOOT)
    error = sysbootrpc(context->route.bootstrap, context->route.claim, &rpc);
  else if (context->route.kind == RCTX_ROUTE_OP)
    error = sysoprpc(context->route.work, context->route.cache, &context->route.operation, &rpc);
  else
    return -ERR_STATE;
  if (error)
    return error;
  for (i = 0; i < 7; i++)
    context->args[i] = 0;
  context->args[0] = (u64) rpc;
  context->number = SYS_RQWAIT;
  for (i = 0; i < sizeof(context->result); i++)
    ((u8 *) &context->result)[i] = 0;
  context->status = 0;
  context->route.rpc = rpc;
  context->route.delivered = 0;
  context->phase = RCTX_PREPARED;
  return 0;
}
isize
sysctxinterruptible(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->phase != RCTX_PREPARED || context->active || context->route.kind != RCTX_ROUTE_OP)
    return -ERR_STATE;
  error = sysopinterruptcheck(context->route.work, context->route.cache, &context->route.operation);
  if (error)
    return error;
  context->route.interruptible = 1;
  return 0;
}
isize
sysctxsignal(Rbank *bank, Connpeer self, Rkey key, void *trap, u64 now)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->route.kind > RCTX_ROUTE_FORK || context->route.interruptible > 1)
    return -ERR_STATE;
  error = sysctxcapture(bank, self, key, trap);
  if (error < 0)
    return error;
  if (context->route.kind == RCTX_ROUTE_FORK)
    return 0;
  if (context->phase == RCTX_DONE && context->route.kind) {
    error = sysctxhandoff(bank, self, key);
    if (error)
      return error;
  }
  if (context->route.pool && context->route.kind == RCTX_ROUTE_OP &&
      context->number == SYS_RQWAIT && context->phase == RCTX_CLEANING &&
      context->route.delivered == 1)
    /* Even when typed capture must defer, the raw tuple and prospective
       references are already owned by the exact Op/Createop. Never wait for
       drainage here or let a service error suppress an application signal. */
    sysctxcreateresult(bank, self, key);
  if (!context->route.interruptible)
    return 0;
  if (context->route.kind != RCTX_ROUTE_OP)
    return -ERR_STATE;
  return sysopinterrupt(context->route.work, context->route.cache, &context->route.operation, now);
}
isize
sysctxsnapshot(Rbank *bank, Connpeer self, Rkey key, u64 offset, u64 bytes)
{
  Rctx *context;
  u64   base;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->phase != RCTX_CLEANING || context->route.kind != RCTX_ROUTE_OP ||
      context->route.delivered != 1 || context->route.snapshot_saved > 1)
    return -ERR_STATE;
  if (context->route.snapshot_saved)
    return context->route.snapshot_offset == offset && context->route.snapshot_bytes == bytes
               ? 0
               : -ERR_STATE;
  base = (context->input_bytes + RCTX_INPUT_ALIGN - 1) & ~((u64) RCTX_INPUT_ALIGN - 1);
  if (bytes > RCTX_SNAPSHOT_BYTES - base)
    return -ERR_CAPACITY;
  error = sysopcopy(context->route.work, context->route.cache, &context->route.operation, offset,
                    bytes, bank->snapshots[key.layer] + base);
  if (error)
    return error;
  context->route.snapshot_offset = offset;
  context->route.snapshot_bytes = bytes;
  context->route.snapshot_saved = 1;
  return 0;
}
isize
sysctxsnapshotview(Rbank *bank, Connpeer self, Rkey key, const void **out, u64 *bytes)
{
  Rctx *context;
  u64   base;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), out, sizeof(*out)) ||
      !separate(bank, sizeof(*bank), bytes, sizeof(*bytes)) ||
      !separate(out, sizeof(*out), bytes, sizeof(*bytes)))
    return -ERR_INVALID;
  if (context->phase != RCTX_CLEANING || context->route.kind != RCTX_ROUTE_OP ||
      context->route.delivered != 1 || context->route.snapshot_saved != 1)
    return -ERR_STATE;
  base = (context->input_bytes + RCTX_INPUT_ALIGN - 1) & ~((u64) RCTX_INPUT_ALIGN - 1);
  if (context->route.snapshot_bytes > RCTX_SNAPSHOT_BYTES - base)
    return -ERR_STATE;
  *out = bank->snapshots[key.layer] + base;
  *bytes = context->route.snapshot_bytes;
  return 0;
}
