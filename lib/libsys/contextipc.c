#include "private/contextipc.h"
#include "private/filecreate.h"
#include "private/syscall.h"
#include <abi/signal.h>
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
      (context->status != -ERR_INTERRUPTED && context->status != -ERR_PEER &&
       context->status != -ERR_CANCELLED && context->status != -ERR_REVOKED) ||
      context->number || context->route.delivered > 1)
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
static int
nativefailure(const Rctx *context, const Slotop *op)
{
  u32 i;
  if ((context->status != -ERR_PEER && context->status != -ERR_CANCELLED &&
       context->status != -ERR_REVOKED) ||
      op->status != context->status || op->phase != SYSOP_RESULT || op->kind || op->resource ||
      (op->opcode != FS_READ && op->opcode != FS_WRITE) ||
      op->flags & (SYSOP_BAD_RESULT | SYSOP_CREATOR | SYSOP_APP_ENDED))
    return 0;
  for (i = 0; i < ARRAY_SIZE(op->words); i++)
    if (op->words[i])
      return 0;
  for (i = 0; i < sizeof(context->result); i++)
    if (((const u8 *) &context->result)[i])
      return 0;
  return 1;
}
static isize
ioresult(u64 opcode, const u64 words[4], u64 bytes)
{
  if ((opcode != FS_READ && opcode != FS_WRITE) || !words[1] || words[3] != bytes ||
      (isize) words[0] > 0)
    return -ERR_STATE;
  if ((isize) words[0] < 0)
    return words[2] || words[3] ? -ERR_STATE : 0;
  if (opcode == FS_READ)
    return words[2] == words[3] && words[3] <= FS_SLOT_BYTES ? 0 : -ERR_STATE;
  return words[3] || words[2] > FS_SLOT_BYTES ? -ERR_STATE : 0;
}
static isize
savedcheck(Rctx *context)
{
  Rroute        expected;
  Ipcmsg        message;
  const Ipcmsg *reply = &message;
  u64 base = (context->input_bytes + RCTX_INPUT_ALIGN - 1) & ~((u64) RCTX_INPUT_ALIGN - 1);
  u32 i;
  for (i = 0; i < sizeof(message); i++)
    ((u8 *) &message)[i] = ((const u8 *) &context->result)[i];
  if (context->phase != RCTX_CLEANING || context->status ||
      context->route.snapshot_bytes > RCTX_SNAPSHOT_BYTES - base || reply->kind || reply->source ||
      reply->request || reply->image ||
      ioresult(context->number, reply->words, context->route.snapshot_bytes))
    return -ERR_STATE;
  clearroute(&expected);
  expected.kind = RCTX_ROUTE_SAVED;
  expected.delivered = expected.snapshot_saved = 1;
  expected.snapshot_bytes = reply->words[3];
  for (i = 0; i < sizeof(expected); i++)
    if (((const u8 *) &expected)[i] != ((const u8 *) &context->route)[i])
      return -ERR_STATE;
  for (i = 0; i < ARRAY_SIZE(context->args); i++)
    if (context->args[i])
      return -ERR_STATE;
  return 0;
}
static isize
forkcomplete(Rctx *context, Connpeer parent)
{
  Rroute *route = &context->route;
  Slotop *op;
  u64     base = (context->input_bytes + RCTX_INPUT_ALIGN - 1) & ~((u64) RCTX_INPUT_ALIGN - 1);
  isize   error;
  if (context->phase != RCTX_CLEANING || context->number != SYS_RQWAIT ||
      route->kind != RCTX_ROUTE_OP || route->delivered != 1 || route->pool ||
      route->snapshot_saved != 1 || route->snapshot_offset ||
      route->snapshot_bytes > RCTX_SNAPSHOT_BYTES - base || !route->work || !route->cache)
    return -ERR_NOTSUP;
  if (route->work->state.self.pid != parent.pid || route->work->state.self.image != parent.image ||
      route->cache->self.pid != parent.pid || route->cache->self.image != parent.image)
    return -ERR_STALE;
  error = sysopcheck(route->work, route->cache, &route->operation);
  if (error)
    return error;
  op = &route->work->state.operations[route->operation.lease.slot];
  if (context->status)
    return nativefailure(context, op) && !route->snapshot_bytes && op->rpc == route->rpc &&
                   (u64) op->rpc == context->args[0]
               ? 0
               : -ERR_STATE;
  if (op->phase != SYSOP_RESULT || op->status ||
      (op->opcode != FS_READ && op->opcode != FS_WRITE) || op->kind != IPC_NONE || op->resource ||
      op->flags & (SYSOP_BAD_RESULT | SYSOP_CREATOR | SYSOP_APP_ENDED) ||
      op->words[1] != route->operation.lease.seq ||
      ioresult(op->opcode, op->words, route->snapshot_bytes) ||
      op->words[3] > (u64) route->operation.lease.span * FS_SLOT_BYTES || op->rpc != route->rpc ||
      (u64) op->rpc != context->args[0])
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
static isize
fixroutes(Rbank *bank, Connpeer parent, const Fixlayer layers[FIX_LAYER_COUNT])
{
  u32   i;
  isize error = sysctxfixcheck(bank, parent, layers);
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
    if (bank->contexts[i].route.kind == RCTX_ROUTE_SAVED) {
      if (layers[i].action != FIX_LAYER_COMPLETE)
        return -ERR_STATE;
      error = savedcheck(&bank->contexts[i]);
      if (error)
        return error;
      continue;
    }
    if (layers[i].action == FIX_LAYER_COMPLETE) {
      error = forkcomplete(&bank->contexts[i], parent);
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
  return 0;
}
isize
sysctxfixplan(Rbank *bank, Connpeer parent, Fixlayer out[FIX_LAYER_COUNT])
{
  Fixlayer layers[FIX_LAYER_COUNT];
  u32      i, j;
  isize    error;
  if (!separate(bank, sizeof(*bank), out, sizeof(layers)) || ((uintptr_t) out & 7) ||
      handlekind(parent.pid) != HANDLE_PROCESS || !parent.image)
    return -ERR_INVALID;
  if (bank->self.pid != parent.pid || bank->self.image != parent.image)
    return -ERR_STALE;
  for (i = 0; i < FIX_LAYER_COUNT; i++) {
    const Rctx *context = &bank->contexts[i];
    layers[i].sequence = context->sequence;
    switch (context->phase) {
    case RCTX_FREE:
      layers[i].sequence = 0;
      layers[i].action = FIX_LAYER_EMPTY;
      break;
    case RCTX_PREPARED:
      layers[i].action = FIX_LAYER_UNSENT;
      break;
    case RCTX_WAITING:
      layers[i].action = FIX_LAYER_INTERRUPT;
      break;
    case RCTX_DONE:
    case RCTX_CLEANING:
      layers[i].action = FIX_LAYER_COMPLETE;
      break;
    default:
      return -ERR_STATE;
    }
    for (j = 0; j < i; j++)
      if (layers[i].sequence && layers[i].sequence == layers[j].sequence)
        return -ERR_STATE;
  }
  error = fixroutes(bank, parent, layers);
  if (error)
    return error;
  for (i = 0; i < FIX_LAYER_COUNT; i++) {
    out[i].sequence = layers[i].sequence;
    out[i].action = layers[i].action;
  }
  return 0;
}
isize
sysctxfixwaitcheck(Rbank *bank, Connpeer parent, Connpeer child,
                   const Fixlayer layers[FIX_LAYER_COUNT])
{
  if (!bank || !layers || handlekind(child.pid) != HANDLE_PROCESS || !child.image ||
      handlekind(parent.pid) != HANDLE_PROCESS || !parent.image || child.pid == parent.pid ||
      !separate(bank, sizeof(*bank), layers, sizeof(*layers) * FIX_LAYER_COUNT))
    return -ERR_INVALID;
  return fixroutes(bank, parent, layers);
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
  error = sysctxfixwaitcheck(bank, parent, child, layers);
  if (error)
    return error;
  for (i = 0; i < RCTX_LAYERS; i++) {
    Rctx *context = &bank->contexts[i];
    if (layers[i].action == FIX_LAYER_COMPLETE && context->route.kind == RCTX_ROUTE_OP &&
        context->status) {
      clearroute(&context->route);
      context->route.kind = RCTX_ROUTE_FORK;
      context->route.delivered = 1;
      context->number = 0;
      for (j = 0; j < ARRAY_SIZE(context->args); j++)
        context->args[j] = 0;
      continue;
    }
    if (layers[i].action == FIX_LAYER_COMPLETE && context->route.kind == RCTX_ROUTE_OP) {
      Slotop *op = &context->route.work->state.operations[context->route.operation.lease.slot];
      u64     bytes = context->route.snapshot_bytes;
      u64     opcode = op->opcode;
      Ipcmsg  reply;
      for (j = 0; j < sizeof(reply); j++)
        ((u8 *) &reply)[j] = 0;
      for (j = 0; j < 4; j++)
        reply.words[j] = op->words[j];
      for (j = 0; j < sizeof(reply); j++)
        ((u8 *) &context->result)[j] = ((const u8 *) &reply)[j];
      clearroute(&context->route);
      context->route.kind = RCTX_ROUTE_SAVED;
      context->route.delivered = 1;
      context->route.snapshot_saved = 1;
      context->route.snapshot_bytes = bytes;
      /* SAVED is a local business result, never a native syscall to enter. */
      context->number = opcode;
      for (j = 0; j < ARRAY_SIZE(context->args); j++)
        context->args[j] = 0;
    }
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
sysctxforkreply(Rbank *bank, Connpeer self, Rkey key, Ipcmsg *out)
{
  Rctx *context;
  u32   i;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), out, sizeof(*out)))
    return -ERR_INVALID;
  if (context->route.kind != RCTX_ROUTE_SAVED)
    return 0;
  error = savedcheck(context);
  if (error)
    return error;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = ((const u8 *) &context->result)[i];
  return 1;
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
static isize
createstep(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
           const Opkey *key, u64 opcode, Rkey *out)
{
  const Slotop *op;
  isize         error = sysfilecreatecheck(pool, work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (op->opcode != opcode || layer >= RCTX_LAYERS || key->lease.slot / 2 != layer)
    return -ERR_INVALID;
  switch (op->phase) {
  case SYSOP_PREPARED:
    return sysctxcreatecall(bank, self, layer, pool, work, cache, key, out);
  case SYSOP_WAITING:
    return sysctxcreatewait(bank, self, layer, pool, work, cache, key, out);
  case SYSOP_RESULT:
  case SYSOP_ACKED:
    return 1;
  default:
    return -ERR_STATE;
  }
}
isize
sysctxpipestep(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
               Conncache *cache, const Opkey *key, Rkey *out)
{
  return createstep(bank, self, layer, pool, work, cache, key, FS_PIPE, out);
}
static isize
bootdrive(Rbank *bank, Connpeer self, u32 layer, Bootstrap *boot, Connclaim claim, Handle call,
          u64 options, Rkey *active, u64 *mask)
{
  Rctx *context;
  u64   ignored;
  isize error, status;
  while (active->sequence || boot->state == SYSBOOT_PREPARED || boot->state == SYSBOOT_WAITING) {
    if (!active->sequence) {
      error = boot->state == SYSBOOT_PREPARED
                  ? sysctxbootcall(bank, self, layer, boot, claim, call, options, active)
                  : sysctxbootwait(bank, self, layer, boot, claim, active);
      if (error)
        return error;
    }
    error = sysctxlookup(bank, self, *active, &context);
    if (error)
      return error;
    if (active->layer != layer || context->route.kind != RCTX_ROUTE_BOOT ||
        context->route.bootstrap != boot || context->route.claim.seq != claim.seq ||
        context->route.claim.owner != claim.owner ||
        context->route.claim.tag.index != claim.tag.index ||
        context->route.claim.tag.generation != claim.tag.generation)
      return -ERR_STALE;
    if (context->phase == RCTX_PREPARED) {
      error = __sys_sigmask(SIGNAL_SETMASK, *mask, &ignored);
      if (error)
        return error;
      status = sysctxinvoke(bank, self, *active);
      error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, mask);
      if (error)
        return error;
    } else
      status = context->status;
    error = sysctxhandoff(bank, self, *active);
    if (!error)
      error = sysctxfinish(bank, self, *active);
    if (error)
      return error;
    active->sequence = 0;
    if (status)
      return status;
  }
  return 0;
}

isize
sysctxhello(Rbank *bank, u32 layer, Connwork *work, Conncache *cache, Handle space, u64 address,
            Bootmap *map, Rkey *active)
{
  Bootstrap *boot;
  Connclaim  claim;
  Connentry  entry;
  Connpeer   self;
  u64        mask, ignored;
  isize      error, status = 0;
  if (layer >= RCTX_LAYERS || handlekind(space) != HANDLE_RESOURCE || (address & PAGE_MASK) ||
      address < BOOT_USER_LO || address >= BOOT_USER_HI || FS_CONN_BYTES > BOOT_USER_HI - address ||
      !separate(bank, sizeof(*bank), work, sizeof(*work)) ||
      !separate(bank, sizeof(*bank), cache, sizeof(*cache)) ||
      !separate(work, sizeof(work->state), cache, sizeof(*cache)) ||
      !separate(active, sizeof(*active), bank, sizeof(*bank)) ||
      !separate(active, sizeof(*active), work, sizeof(*work)) ||
      !separate(active, sizeof(*active), cache, sizeof(*cache)) ||
      !separate(map, sizeof(*map), bank, sizeof(*bank)) ||
      !separate(map, sizeof(*map), work, sizeof(*work)) ||
      !separate(map, sizeof(*map), cache, sizeof(*cache)) ||
      !separate(map, sizeof(*map), active, sizeof(*active)))
    return -ERR_INVALID;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  boot = &work->state.bootstrap;
  claim = boot->claim;
  self = work->state.self;
  error = sysconncheck(cache, self);
  if (!error)
    error = sysconnview(cache, work->state.tag, &entry);
  if (error)
    goto restore;
  if (entry.state != SYSCONN_CONNECTING || entry.owner != claim.owner ||
      entry.sequence != claim.seq || claim.tag.generation != work->state.tag.generation ||
      claim.tag.index != work->state.tag.index || entry.peer.pid != work->state.peer.pid ||
      entry.peer.image != work->state.peer.image || boot->opcode != FS_HELLO) {
    error = -ERR_STALE;
    goto restore;
  }
  error = bootdrive(bank, self, layer, boot, claim, work->state.call, CALL_RECV_MEM, active, &mask);
  if (!error)
    error = sysbootfinished(boot, claim);
  if (!error && !map->mapped)
    error = sysbootmap(boot, claim, space, address, work->state.peer.image, map);
  if (!error && (map->address != address || map->verified != 1))
    error = -ERR_INVALID;
  if (!error && boot->state == SYSBOOT_RESULT)
    error = sysbootack(boot, claim);
  if (!error)
    error = sysconnworkadopt(work, cache, claim, space, map);
  if (!error)
    error = sysbootfinish(boot, claim);
restore:
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : status;
}

static isize
createio(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
         const Opkey *key, u64 opcode, Rkey *active)
{
  Rctx *context;
  u64   mask, ignored;
  isize error, status = 0;
  if (!active || !separate(bank, sizeof(*bank), active, sizeof(*active)) ||
      !separate(key, sizeof(*key), active, sizeof(*active)))
    return -ERR_INVALID;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysfilecreatecheck(pool, work, cache, key);
  if (error)
    goto restore;
  if (work->state.operations[key->lease.slot].opcode != opcode || layer >= RCTX_LAYERS ||
      key->lease.slot / 2 != layer) {
    error = -ERR_INVALID;
    goto restore;
  }
  if (!active->sequence) {
    error = createstep(bank, self, layer, pool, work, cache, key, opcode, active);
    if (error)
      goto restore;
  }
  error = sysctxlookup(bank, self, *active, &context);
  if (error)
    goto restore;
  if (active->layer != layer || context->route.kind != RCTX_ROUTE_OP ||
      context->route.pool != pool || context->route.work != work || context->route.cache != cache ||
      context->route.operation.lease.seq != key->lease.seq ||
      context->route.operation.lease.slot != key->lease.slot ||
      context->route.operation.owner != key->owner) {
    error = -ERR_STALE;
    goto restore;
  }
  if (context->phase == RCTX_PREPARED) {
    error = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    if (error)
      return error;
    status = sysctxinvoke(bank, self, *active);
    error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
    if (error)
      return error;
  } else
    status = context->status;
  error = sysctxhandoff(bank, self, *active);
  if (!error && status == -ERR_PEER && context->number == SYS_CALL)
    error = sysctxpeerended(bank, self, *active);
  if (!error)
    error = sysctxfinish(bank, self, *active);
  if (!error) {
    active->sequence = 0;
    error = status;
  }
restore:
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return status ? status : error;
}

isize
sysctxpipeio(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
             Conncache *cache, const Opkey *key, Rkey *active)
{
  return createio(bank, self, layer, pool, work, cache, key, FS_PIPE, active);
}

isize
sysctxopenio(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
             Conncache *cache, const Opkey *key, Rkey *active)
{
  return createio(bank, self, layer, pool, work, cache, key, FS_OPEN, active);
}

isize
sysctxpipecreate(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                 Fdtable *table, Pipecreate *pipe, Rkey *active, int *fds, isize *business)
{
  Connpeer self;
  Msg      message;
  u64      mask, ignored;
  isize    error, status;
  if (!bank || !work || !pipe || !active || layer >= RCTX_LAYERS ||
      !separate(pipe, sizeof(*pipe), active, sizeof(*active)))
    return -ERR_INVALID;
  self = work->state.self;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (!pipe->operation.lease.seq) {
    if (active->sequence || bank->contexts[layer].phase != RCTX_FREE)
      error = -ERR_BUSY;
    else if (bank->sequence == UINT64_MAX)
      error = -ERR_OVERFLOW;
    else
      error =
          sysfilepipeprepare(pool, work, cache, table, layer, bank->sequence + 1, pipe, &message);
  }
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (error || status)
    return error ? error : status;
  do {
    error = sysctxpipeio(bank, self, layer, pool, work, cache, &pipe->operation, active);
  } while (!error);
  if (active->sequence)
    return error;
  status = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (status)
    return status;
  if (!sysfilecreatecheck(pool, work, cache, &pipe->operation)) {
    u32 phase = work->state.operations[pipe->operation.lease.slot].phase;
    if (error == 1 || error == -ERR_CANCELLED || (error == -ERR_PEER && phase == SYSOP_RESULT))
      error = sysfilepipefinish(pool, work, cache, table, pipe, fds, business);
    else if (phase == SYSOP_PREPARED) {
      status = sysfilepipeabort(pool, work, cache, table, pipe);
      if (status)
        error = status;
    }
  }
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : status;
}

isize
sysctxopencreate(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                 Fdtable *table, const u64 args[8], const void *path, u64 bytes, u64 flags,
                 Opencreate *open, Rkey *active, int *fd, isize *business)
{
  Connpeer self;
  Msg      message;
  u64      mask, ignored;
  isize    error, status;
  if (!bank || !work || !open || !active || layer >= RCTX_LAYERS ||
      !separate(open, sizeof(*open), active, sizeof(*active)) ||
      !separate(fd, sizeof(*fd), business, sizeof(*business)) ||
      !separate(fd, sizeof(*fd), bank, sizeof(*bank)) ||
      !separate(fd, sizeof(*fd), open, sizeof(*open)) ||
      !separate(fd, sizeof(*fd), active, sizeof(*active)) ||
      !separate(fd, sizeof(*fd), table, sizeof(*table)) ||
      !separate(fd, sizeof(*fd), pool, sizeof(*pool)) ||
      !separate(fd, sizeof(*fd), work, sizeof(*work)) ||
      !separate(fd, sizeof(*fd), cache, sizeof(*cache)) ||
      !separate(business, sizeof(*business), bank, sizeof(*bank)) ||
      !separate(business, sizeof(*business), active, sizeof(*active)))
    return -ERR_INVALID;
  self = work->state.self;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (!open->operation.lease.seq) {
    if (active->sequence || bank->contexts[layer].phase != RCTX_FREE)
      error = -ERR_BUSY;
    else if (bank->sequence == UINT64_MAX)
      error = -ERR_OVERFLOW;
    else
      error = sysfileopenprepare(pool, work, cache, table, layer, bank->sequence + 1, args, path,
                                 bytes, flags, open, &message);
  }
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (error || status)
    return error ? error : status;
  do {
    error = sysctxopenio(bank, self, layer, pool, work, cache, &open->operation, active);
  } while (!error);
  if (active->sequence)
    return error;
  status = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (status)
    return status;
  if (!sysfilecreatecheck(pool, work, cache, &open->operation)) {
    Slotop *op = &work->state.operations[open->operation.lease.slot];
    if (error == 1 || error == -ERR_CANCELLED ||
        (error == -ERR_PEER && op->phase == SYSOP_RESULT)) {
      if (op->status == -ERR_PEER) {
        error = work->state.ended ? 0 : sysoppeerended(work, cache, &open->operation);
        if (!error)
          error = sysfilecreatefailed(pool, work, cache, &open->operation);
      } else
        error = 0;
      if (!error)
        error = sysfilecreateack(pool, work, cache, &open->operation);
      if (!error && work->state.creators[open->operation.lease.slot].token)
        error = 1;
      if (!error)
        error = sysfilecreatesettle(pool, work, cache, &open->operation, 0);
      if (!error) {
        error = sysfileopenfinish(pool, work, cache, table, open, business);
        if (error >= 0) {
          *fd = *business ? -1 : (int) error;
          error = 0;
        }
      }
    } else if (op->phase == SYSOP_PREPARED) {
      status = sysfileopenabort(pool, work, cache, table, open);
      if (status)
        error = status;
    }
  }
  status = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return status ? status : error;
}

isize
sysctxopenattach(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                 const Opkey *key, Connwork *backend, Handle space, u64 address, u64 deadline,
                 Bootmap *map, Rkey *active)
{
  Bootstrap *boot;
  Createop  *create;
  Connclaim  claim;
  Connentry  entry;
  Bootreply  reply;
  Handle     call;
  u64        mask, ignored;
  isize      error, restored;
  if (!bank || !backend || layer >= RCTX_LAYERS || handlekind(space) != HANDLE_RESOURCE ||
      !separate(bank, sizeof(*bank), backend, sizeof(*backend)) ||
      !separate(bank, sizeof(*bank), map, sizeof(*map)) ||
      !separate(bank, sizeof(*bank), active, sizeof(*active)) ||
      !separate(map, sizeof(*map), active, sizeof(*active)) ||
      !separate(map, sizeof(*map), backend, sizeof(*backend)) ||
      !separate(map, sizeof(*map), work, sizeof(*work)) ||
      !separate(map, sizeof(*map), pool, sizeof(*pool)) ||
      !separate(map, sizeof(*map), cache, sizeof(*cache)) ||
      !separate(map, sizeof(*map), key, sizeof(*key)) ||
      !separate(active, sizeof(*active), backend, sizeof(*backend)) ||
      !separate(active, sizeof(*active), work, sizeof(*work)) ||
      !separate(active, sizeof(*active), pool, sizeof(*pool)) ||
      !separate(active, sizeof(*active), cache, sizeof(*cache)) ||
      !separate(active, sizeof(*active), key, sizeof(*key)))
    return -ERR_INVALID;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysfilecreatecheck(pool, work, cache, key);
  if (error)
    goto restore;
  create = &work->state.creators[key->lease.slot];
  if (key->lease.slot / 2 != layer || create->phase != SYSFILE_CREATE_CAPTURED ||
      work->state.operations[key->lease.slot].phase != SYSOP_ACKED ||
      work->state.operations[key->lease.slot].kind != IPC_RESOURCE_REPLY || !create->token ||
      backend->state.peer.pid != create->peer.pid ||
      backend->state.peer.image != create->peer.image) {
    error = -ERR_STALE;
    goto restore;
  }
  boot = &backend->state.bootstrap;
  if (boot->state == SYSBOOT_FREE && !active->sequence) {
    error = sysfilecreateattachprepare(pool, work, cache, key, backend, deadline, &claim, &call);
    if (error)
      goto restore;
  } else {
    claim = boot->claim;
    call = create->call;
  }
  error = sysconnview(cache, backend->state.tag, &entry);
  if (error)
    goto restore;
  if (boot->opcode != FS_ATTACH || boot->token != create->token ||
      claim.owner != create->files[0] || claim.tag.index != backend->state.tag.index ||
      claim.tag.generation != backend->state.tag.generation || entry.sequence != claim.seq ||
      (entry.state != SYSCONN_READY && entry.state != SYSCONN_CONNECTING) ||
      (entry.owner != claim.owner && (entry.owner || entry.state != SYSCONN_READY ||
                                      boot->state != SYSBOOT_ACKED || active->sequence))) {
    error = -ERR_STALE;
    goto restore;
  }
  if (!entry.owner) {
    error = sysfilecreateattached(pool, work, cache, key, backend, claim, space, map, 0);
    goto restore;
  }
  error = bootdrive(bank, work->state.self, layer, boot, claim, call, CALL_RECV_MEM, active, &mask);
  if (!error)
    error = sysbootfinished(boot, claim);
  if (!error)
    error = sysbootreply(boot, claim, entry.state == SYSCONN_READY ? entry.connid : 0, &reply);
  if (!error && entry.state == SYSCONN_CONNECTING && !map->mapped)
    error = sysbootmap(boot, claim, space, address, create->peer.image, map);
  if (!error && entry.state == SYSCONN_CONNECTING && (map->address != address || !map->verified))
    error = -ERR_INVALID;
  if (!error && boot->state == SYSBOOT_RESULT)
    error = sysbootack(boot, claim);
  if (!error)
    error = sysfilecreateattached(pool, work, cache, key, backend, claim, space, map, 0);
restore:
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : error;
}

isize
sysctxopenrevoke(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                 const Opkey *key, u64 deadline, Rkey *active)
{
  Bootstrap *boot;
  Createop  *create;
  Connclaim  claim;
  Connpeer   peer;
  Handle     call;
  u64        mask, ignored;
  isize      error, restored;
  if (!bank || layer >= RCTX_LAYERS || !storage(bank, work, sizeof(*work), active) ||
      !storage(bank, pool, sizeof(*pool), active) ||
      !storage(bank, cache, sizeof(*cache), active) || !storage(bank, key, sizeof(*key), active) ||
      !separate(bank, sizeof(*bank), active, sizeof(*active)))
    return -ERR_INVALID;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysfilecreatecheck(pool, work, cache, key);
  if (error)
    goto restore;
  create = &work->state.creators[key->lease.slot];
  boot = &work->state.bootstrap;
  if (key->lease.slot / 2 != layer || create->phase != SYSFILE_CREATE_CAPTURED ||
      work->state.operations[key->lease.slot].phase != SYSOP_ACKED ||
      work->state.operations[key->lease.slot].kind != IPC_RESOURCE_REPLY || !create->token) {
    error = -ERR_STALE;
    goto restore;
  }
  call = create->call;
  if (boot->state == SYSBOOT_FREE && !active->sequence) {
    error = sysfilecreaterevokeprepare(pool, work, cache, key, call, deadline, &claim, &call);
    if (error)
      goto restore;
  } else {
    claim = boot->claim;
  }
  if (sysbootcheck(boot) || boot->opcode != FS_ABORT || boot->token != create->token ||
      claim.owner != create->files[0] || claim.tag.index != SYSCONN_COUNT || claim.tag.generation) {
    error = -ERR_STALE;
    goto restore;
  }
  if (boot->state == SYSBOOT_PREPARED) {
    error = sysconnpeer(call, &peer);
    if (error)
      goto restore;
    if (peer.pid != create->peer.pid || peer.image != create->peer.image) {
      error = -ERR_DENIED;
      goto restore;
    }
    if (active->sequence) {
      Rctx *context;
      error = sysctxlookup(bank, work->state.self, *active, &context);
      if (error)
        goto restore;
      if (context->number != SYS_CALL || context->args[0] != (u64) call) {
        error = -ERR_STALE;
        goto restore;
      }
    }
  }
  /* An ABORT with zero extra options selects the control cleanup budget. */
  error = bootdrive(bank, work->state.self, layer, boot, claim, call, 0, active, &mask);
  if (!error)
    error = sysbootfinished(boot, claim);
  if (!error)
    error = sysbootabortreply(boot, claim);
  if (!error && boot->state == SYSBOOT_RESULT)
    error = sysbootack(boot, claim);
  if (!error)
    error = sysfilecreaterevoked(pool, work, cache, key, claim);
restore:
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : error;
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
  if (context->route.kind > RCTX_ROUTE_SAVED || context->route.interruptible > 1)
    return -ERR_STATE;
  error = sysctxcapture(bank, self, key, trap);
  if (error < 0)
    return error;
  if (context->route.kind == RCTX_ROUTE_FORK || context->route.kind == RCTX_ROUTE_SAVED)
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
  error = sysopcheck(context->route.work, context->route.cache, &context->route.operation);
  if (error)
    return error;
  {
    Slotop *op = &context->route.work->state.operations[context->route.operation.lease.slot];
    if (!offset && !bytes && nativefailure(context, op))
      error = sysopfinished(context->route.work, context->route.cache, &context->route.operation);
    else if (!offset && !bytes && !op->status && op->kind == IPC_NONE && !op->resource &&
             !(op->flags & (SYSOP_BAD_RESULT | SYSOP_CREATOR | SYSOP_APP_ENDED)) &&
             (isize) op->words[0] < 0 && op->words[1] == context->route.operation.lease.seq &&
             !ioresult(op->opcode, op->words, 0))
      error = sysopfinished(context->route.work, context->route.cache, &context->route.operation);
    else
      error = sysopcopy(context->route.work, context->route.cache, &context->route.operation,
                        offset, bytes, bank->snapshots[key.layer] + base);
  }
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
  if (context->phase != RCTX_CLEANING ||
      (context->route.kind != RCTX_ROUTE_OP && context->route.kind != RCTX_ROUTE_SAVED) ||
      context->route.delivered != 1 || context->route.snapshot_saved != 1)
    return -ERR_STATE;
  base = (context->input_bytes + RCTX_INPUT_ALIGN - 1) & ~((u64) RCTX_INPUT_ALIGN - 1);
  if (context->route.snapshot_bytes > RCTX_SNAPSHOT_BYTES - base)
    return -ERR_STATE;
  *out = bank->snapshots[key.layer] + base;
  *bytes = context->route.snapshot_bytes;
  return 0;
}
