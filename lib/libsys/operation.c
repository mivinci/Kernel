#include "private/operation.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static int
output(Connwork *work, Conncache *cache, const void *out, u64 bytes)
{
  return separate(work, sizeof(*work), out, bytes) && separate(cache, sizeof(*cache), out, bytes);
}
static void
clear(Slotop *op)
{
  u32 i;
  for (i = 0; i < sizeof(*op); i++)
    ((u8 *) op)[i] = 0;
}
static int
terminal(isize error)
{
  return !error || error == -ERR_CANCELLED || error == -ERR_PEER || error == -ERR_REVOKED;
}
static isize
match(Connwork *work, Conncache *cache, const Opkey *key, Slotop **out)
{
  Connentry entry;
  Slotop   *op;
  u32       i;
  isize     error;
  if (!work || !cache || !key || !output(work, cache, key, sizeof(*key)) ||
      key->connection.reserved || key->lease.slot >= FS_CONN_SLOTS || !key->lease.seq ||
      !key->lease.span || key->lease.span > 2)
    return -ERR_INVALID;
  if (work->state.self.pid != cache->self.pid || work->state.self.image != cache->self.image ||
      key->connection.index != work->state.tag.index ||
      key->connection.generation != work->state.tag.generation)
    return -ERR_STALE;
  error = sysconncheck(cache, work->state.self);
  if (!error)
    error = sysconnview(cache, key->connection, &entry);
  if (error)
    return error;
  if (entry.peer.pid != work->state.peer.pid || entry.peer.image != work->state.peer.image ||
      (entry.state != SYSCONN_READY && entry.state != SYSCONN_DRAINING) ||
      sysfilepagescheck(&work->state.pages))
    return -ERR_STATE;
  op = &work->state.operations[key->lease.slot];
  if (!op->phase || op->lease.seq != key->lease.seq || op->lease.slot != key->lease.slot ||
      op->lease.span != key->lease.span || op->owner != key->owner)
    return -ERR_STALE;
  if (op->phase > SYSOP_ACKED || !op->file ||
      op->flags & ~(SYSOP_BACKGROUND | SYSOP_SAVED | SYSOP_RESOURCE_MOVED | SYSOP_BAD_RESULT) ||
      ((op->flags & SYSOP_BACKGROUND) ? op->owner != 0 : op->owner == 0) ||
      ((op->phase == SYSOP_WAITING || op->phase == SYSOP_RESULT) ? handlekind(op->rpc) != HANDLE_RPC
                                                                 : op->rpc != 0))
    return -ERR_STATE;
  for (i = 0; i < key->lease.span; i++) {
    const Filelease *page;
    if (key->lease.slot + i >= FS_CONN_SLOTS)
      return -ERR_STATE;
    page = &work->state.pages.pages[key->lease.slot + i];
    if (page->seq != key->lease.seq || page->slot != key->lease.slot ||
        page->span != key->lease.span)
      return -ERR_STATE;
  }
  *out = op;
  return 0;
}
isize
sysopprepare(Connwork *work, Conncache *cache, u32 context, u32 span, u64 owner, u64 *owned_file,
             u64 object, u64 opcode, u64 deadline, Opkey *out)
{
  Filelease lease;
  Slotop   *op;
  isize     error;
  if (!owner || !output(work, cache, owned_file, sizeof(*owned_file)) || !*owned_file ||
      !output(work, cache, out, sizeof(*out)) ||
      !separate(owned_file, sizeof(*owned_file), out, sizeof(*out)) || opcode < FS_OPEN ||
      opcode > FS_PIPE || opcode == FS_ATTACH || opcode == FS_EXEC_CLAIM || opcode == FS_ABORT ||
      (opcode >= 13 && opcode <= 15) || (context >= SYSFILE_CONTROL && opcode != FS_CLOSE) ||
      (span == 2 && opcode != FS_PATH && opcode != FS_FORK_SNAPSHOT && opcode != FS_FORK_PREPARE &&
       opcode != FS_EXEC_PREPARE))
    return -ERR_INVALID;
  error = sysconnworkreserve(work, cache, context, span, &lease);
  if (error)
    return error;
  op = &work->state.operations[lease.slot];
  if (op->phase) {
    if (sysfilerelease(&work->state.pages, lease))
      return -ERR_STATE;
    return -ERR_STATE;
  }
  clear(op);
  op->lease.seq = lease.seq;
  op->lease.slot = lease.slot;
  op->lease.span = lease.span;
  op->owner = owner;
  op->file = *owned_file;
  *owned_file = 0;
  op->object = object;
  op->opcode = opcode;
  op->deadline = deadline;
  op->phase = SYSOP_PREPARED;
  out->connection.generation = work->state.tag.generation;
  out->connection.index = work->state.tag.index;
  out->connection.reserved = 0;
  out->lease.seq = lease.seq;
  out->lease.slot = lease.slot;
  out->lease.span = lease.span;
  out->owner = owner;
  return 0;
}
isize
sysopsubmitted(Connwork *work, Conncache *cache, const Opkey *key, Handle *owned_rpc)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!output(work, cache, owned_rpc, sizeof(*owned_rpc)))
    return -ERR_INVALID;
  if (op->phase != SYSOP_PREPARED || handlekind(*owned_rpc) != HANDLE_RPC)
    return -ERR_STATE;
  op->rpc = *owned_rpc;
  *owned_rpc = 0;
  op->phase = SYSOP_WAITING;
  return 0;
}
isize
sysopcapture(Connwork *work, Conncache *cache, const Opkey *key, isize status, const Ipcmsg *result)
{
  Slotop *op;
  u32     i;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_WAITING)
    return -ERR_STATE;
  if (!terminal(status))
    return status;
  if (!output(work, cache, result, sizeof(*result)))
    return -ERR_INVALID;
  op->status = status;
  op->kind = result->kind;
  for (i = 0; i < 4; i++)
    op->words[i] = result->words[i];
  if (result->kind == IPC_RESOURCE_REPLY &&
      handlekind((Handle) result->words[0]) == HANDLE_RESOURCE)
    op->resource = (Handle) result->words[0];
  if (result->source || result->request || result->image ||
      (result->kind != IPC_NONE && result->kind != IPC_RESOURCE_REPLY) ||
      (result->kind == IPC_RESOURCE_REPLY && (status || !op->resource)))
    op->flags |= SYSOP_BAD_RESULT;
  op->phase = SYSOP_RESULT;
  return 0;
}
isize
sysoppoll(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  Ipcmsg  result;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase == SYSOP_RESULT || op->phase == SYSOP_ACKED)
    return 0;
  if (op->phase != SYSOP_WAITING)
    return -ERR_STATE;
  error = ipc_rqpoll(op->rpc, 0, &result);
  return sysopcapture(work, cache, key, error, &result);
}
isize
sysopcancel(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  return op->phase == SYSOP_WAITING ? ipc_rqcancel(op->rpc) : -ERR_STATE;
}
isize
sysopbackground(Connwork *work, Conncache *cache, Opkey *owned_key, Opkey *out)
{
  Slotop *op;
  isize   error;
  u32     i;
  if (!output(work, cache, owned_key, sizeof(*owned_key)) ||
      !output(work, cache, out, sizeof(*out)) ||
      !separate(owned_key, sizeof(*owned_key), out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(work, cache, owned_key, &op);
  if (error)
    return error;
  if (op->phase == SYSOP_PREPARED || op->flags & SYSOP_BACKGROUND)
    return -ERR_STATE;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = ((const u8 *) owned_key)[i];
  out->owner = op->owner = 0;
  op->flags |= SYSOP_BACKGROUND;
  for (i = 0; i < sizeof(*owned_key); i++)
    ((u8 *) owned_key)[i] = 0;
  return 0;
}
isize
sysopsaved(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_RESULT)
    return -ERR_STATE;
  op->flags |= SYSOP_SAVED;
  return 0;
}
isize
sysopack(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_RESULT || !(op->flags & SYSOP_SAVED))
    return -ERR_STATE;
  error = ipc_rqack(op->rpc, ACK_RESULT | ACK_RELEASE);
  if (error)
    return error;
  op->rpc = 0;
  op->phase = SYSOP_ACKED;
  return 0;
}
isize
sysopresource(Connwork *work, Conncache *cache, const Opkey *key, Handle *out)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!output(work, cache, out, sizeof(*out)))
    return -ERR_INVALID;
  if ((op->phase != SYSOP_RESULT && op->phase != SYSOP_ACKED) || !op->resource)
    return -ERR_STATE;
  *out = op->resource;
  op->resource = 0;
  op->flags |= SYSOP_RESOURCE_MOVED;
  return 0;
}
static isize
finish(Connwork *work, Conncache *cache, const Opkey *key, u64 *file, u32 phase)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!output(work, cache, file, sizeof(*file)))
    return -ERR_INVALID;
  if (op->phase != phase || op->rpc || op->resource ||
      (phase == SYSOP_ACKED && !(op->flags & SYSOP_SAVED)))
    return -ERR_BUSY;
  error = sysfilerelease(&work->state.pages, key->lease);
  if (error)
    return error;
  *file = op->file;
  clear(op);
  return 0;
}
isize
sysopfinish(Connwork *work, Conncache *cache, const Opkey *key, u64 *file)
{
  return finish(work, cache, key, file, SYSOP_ACKED);
}
isize
sysopabort(Connwork *work, Conncache *cache, const Opkey *key, u64 *file)
{
  return finish(work, cache, key, file, SYSOP_PREPARED);
}
