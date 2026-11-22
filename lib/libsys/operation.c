#include "private/operation.h"
#include "private/syscall.h"
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
  if (op->phase > SYSOP_ACKED ||
      (!op->file && !(op->opcode == FS_PATH && !op->object) &&
       !(op->opcode == FS_LOAD && op->object)) ||
      op->flags & ~(SYSOP_BACKGROUND | SYSOP_SAVED | SYSOP_RESOURCE_MOVED | SYSOP_BAD_RESULT |
                    SYSOP_CANCEL_SENT | SYSOP_APP_ENDED | SYSOP_CREATOR) ||
      ((op->flags & SYSOP_BACKGROUND) ? op->owner != 0 : op->owner == 0) ||
      ((op->phase == SYSOP_WAITING || op->phase == SYSOP_RESULT) ? handlekind(op->rpc) != HANDLE_RPC
                                                                 : op->rpc != 0))
    return -ERR_STATE;
  if (op->flags & SYSOP_CANCEL_SENT && (!op->deadline || op->phase == SYSOP_PREPARED))
    return -ERR_STATE;
  if (op->flags & SYSOP_APP_ENDED && !(op->flags & SYSOP_BACKGROUND))
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
sysopcheck(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  return match(work, cache, key, &op);
}
isize
sysopprepare(Connwork *work, Conncache *cache, u32 context, u32 span, u64 owner, u64 *owned_file,
             u64 object, u64 opcode, u64 deadline, Opkey *out)
{
  Filelease lease;
  Slotop   *op;
  isize     error;
  if (!owner || !output(work, cache, owned_file, sizeof(*owned_file)) ||
      (!*owned_file && !(opcode == FS_PATH && !object) && !(opcode == FS_LOAD && object)) ||
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
  if (op->phase || work->state.creators[lease.slot].phase) {
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
sysoprequest(Connwork *work, Conncache *cache, const Opkey *key, Msg *out)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!output(work, cache, out, sizeof(*out)) || !separate(key, sizeof(*key), out, sizeof(*out)))
    return -ERR_INVALID;
  if (op->phase != SYSOP_PREPARED)
    return -ERR_STATE;
  out->words[0] = op->opcode;
  out->words[1] = work->state.map.connid;
  out->words[2] = key->lease.seq;
  out->words[3] = key->lease.slot;
  return 0;
}
isize
sysoprpc(Connwork *work, Conncache *cache, const Opkey *key, Handle *out)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!output(work, cache, out, sizeof(*out)) || !separate(key, sizeof(*key), out, sizeof(*out)))
    return -ERR_INVALID;
  if (op->phase != SYSOP_WAITING)
    return -ERR_STATE;
  *out = op->rpc;
  return 0;
}

static int
pollopcode(u64 opcode)
{
  return opcode == FS_POLL_QUERY || opcode == FS_POLL_WAIT;
}

isize
sysoppollprepare(Connwork *work, Conncache *cache, u32 context, u64 owner, u64 *owned_file,
                 u64 opcode, u64 deadline, const Filepoll *rows, u32 count, Opkey *out)
{
  Opkey draft;
  isize error;
  u32   i;
  if (!pollopcode(opcode) || !output(work, cache, out, sizeof(*out)) ||
      !separate(owned_file, sizeof(*owned_file), out, sizeof(*out)))
    return -ERR_INVALID;
  error = sysopprepare(work, cache, context, 1, owner, owned_file, count, opcode, 0, &draft);
  if (error)
    return error;
  error = sysfilepollstage((void *) (uintptr_t) work->state.map.address, work->state.map.bytes,
                           draft.lease, rows, count, deadline, owner);
  if (error)
    return sysopabort(work, cache, &draft, owned_file) ? -ERR_STATE : error;
  for (i = 0; i < sizeof(draft); i++)
    ((volatile u8 *) out)[i] = ((const u8 *) &draft)[i];
  return 0;
}

isize
sysoppollabandon(Connwork *work, Conncache *cache, Opkey *key, Opkey *background)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!pollopcode(op->opcode) || !key->owner || op->phase == SYSOP_PREPARED ||
      !output(work, cache, background, sizeof(*background)) ||
      !separate(key, sizeof(*key), background, sizeof(*background)))
    return -ERR_INVALID;
  if (op->phase == SYSOP_WAITING && !(op->flags & SYSOP_CANCEL_SENT)) {
    error = sysopcancel(work, cache, key);
    if (error)
      return error;
  }
  return sysopabandon(work, cache, key, background);
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
sysoppeerended(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  Ipcmsg  result;
  u32     i;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_WAITING && op->phase != SYSOP_RESULT)
    return -ERR_STATE;
  error = ipc_rqpoll(op->rpc, 0, &result);
  if (error != -ERR_PEER)
    return error ? error : -ERR_STATE;
  if (result.kind || result.source || result.request || result.image)
    return -ERR_STATE;
  for (i = 0; i < 4; i++)
    if (result.words[i])
      return -ERR_STATE;
  error = sysconninvalidate(cache, key->connection);
  if (!error)
    work->state.ended = 1;
  return error;
}
static isize
interruptible(const Slotop *op)
{
  /* CLOSE keeps its mandatory cleanup path after the descriptor is detached. */
  if (op->flags & SYSOP_BACKGROUND ||
      (op->opcode != FS_READ && op->opcode != FS_WRITE && op->opcode != FS_SEEK &&
       op->opcode != FS_TTYCTL && op->opcode != FS_STAT && op->opcode != FS_SYNC &&
       op->opcode != FS_OPEN && op->opcode != FS_PATH && op->opcode != FS_POLL_QUERY &&
       op->opcode != FS_POLL_WAIT))
    return -ERR_NOTSUP;
  if (!(op->flags & SYSOP_CANCEL_SENT) && op->deadline)
    return -ERR_STATE;
  return 0;
}
isize
sysopinterruptcheck(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  return error ? error : interruptible(op);
}
isize
sysopinterrupt(Connwork *work, Conncache *cache, const Opkey *key, u64 now)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  error = interruptible(op);
  if (error)
    return error;
  if (op->phase != SYSOP_WAITING || op->flags & SYSOP_CANCEL_SENT)
    return 0;
  if (now > UINT64_MAX - SYSOP_CONTROL_NS)
    return -ERR_OVERFLOW;
  error = ipc_rqcancel(op->rpc);
  if (error)
    return error;
  op->deadline = now + SYSOP_CONTROL_NS;
  op->flags |= SYSOP_CANCEL_SENT;
  return 0;
}
static isize
ended(const Slotop *op)
{
  Ipcmsg result;
  u32    i;
  isize  error = ipc_rqpoll(op->rpc, RQ_FINISH, &result);
  if (error)
    return error;
  if (result.kind || result.source || result.request || result.image)
    return -ERR_STATE;
  for (i = 0; i < 4; i++)
    if (result.words[i])
      return -ERR_STATE;
  return 0;
}
isize
sysopfinished(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase == SYSOP_ACKED)
    return 0;
  return op->phase == SYSOP_RESULT ? ended(op) : -ERR_STATE;
}
isize
sysopcopy(Connwork *work, Conncache *cache, const Opkey *key, u64 offset, u64 bytes, void *out)
{
  Slotop *op;
  u64     limit;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_RESULT || op->status || op->words[1] != key->lease.seq ||
      op->flags & SYSOP_BAD_RESULT)
    return -ERR_STATE;
  if (op->kind == IPC_NONE) {
    if (op->resource || op->words[0])
      return -ERR_STATE;
  } else if (op->kind != IPC_RESOURCE_REPLY || op->opcode != FS_OPEN || !op->resource ||
             !op->words[2] || !op->words[3] || offset || bytes > 8)
    return -ERR_STATE;
  limit = (u64) key->lease.span * FS_SLOT_BYTES;
  if (!work->state.map.mapped || !work->state.map.verified ||
      work->state.map.bytes != FS_CONN_BYTES ||
      work->state.map.address > UINT64_MAX - FS_CONN_BYTES || offset > limit ||
      bytes > limit - offset || (op->kind == IPC_NONE && bytes > op->words[3]) ||
      !output(work, cache, out, bytes) || !separate(key, sizeof(*key), out, bytes) ||
      !separate((void *) (uintptr_t) work->state.map.address, FS_CONN_BYTES, out, bytes))
    return -ERR_INVALID;
  error = ended(op);
  if (error)
    return error;
  return __sys_copy(out,
                    (const void *) (uintptr_t) (work->state.map.address + FS_DATA_OFFSET +
                                                key->lease.slot * FS_SLOT_BYTES + offset),
                    bytes);
}
isize
sysopresume(Connwork *work, Conncache *cache, Opkey *owned_key, u64 now, Opkey *out)
{
  Slotop *op;
  isize   error;
  if (!output(work, cache, owned_key, sizeof(*owned_key)) ||
      !output(work, cache, out, sizeof(*out)) ||
      !separate(owned_key, sizeof(*owned_key), out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(work, cache, owned_key, &op);
  if (!error)
    error = interruptible(op);
  if (error)
    return error;
  error = sysoppoll(work, cache, owned_key);
  if (error != -ERR_WOULD_BLOCK)
    return error;
  if (!(op->flags & SYSOP_CANCEL_SENT) || now < op->deadline)
    return -ERR_WOULD_BLOCK;
  error = sysopabandon(work, cache, owned_key, out);
  if (error)
    return error;
  return SYSOP_ABANDONED;
}
isize
sysopdrain(Connwork *work, Conncache *cache, const Opkey *key, u64 *file)
{
  Slotop *op;
  isize   error;
  if (!output(work, cache, file, sizeof(*file)) || !file || *file ||
      !separate(key, sizeof(*key), file, sizeof(*file)))
    return -ERR_INVALID;
  error = match(work, cache, key, &op);
  if (error)
    return error;
  if (!(op->flags & SYSOP_APP_ENDED) || !(op->flags & SYSOP_BACKGROUND) ||
      (op->opcode != FS_READ && op->opcode != FS_WRITE && op->opcode != FS_SEEK &&
       op->opcode != FS_TTYCTL && op->opcode != FS_STAT && op->opcode != FS_SYNC &&
       op->opcode != FS_PATH && !pollopcode(op->opcode)))
    return -ERR_NOTSUP;
  if (op->phase == SYSOP_WAITING) {
    error = sysoppoll(work, cache, key);
    if (error)
      return error;
  }
  if (op->phase == SYSOP_RESULT) {
    error = ended(op);
    if (error)
      return error;
    if (op->resource) {
      error = __sys_free(op->resource);
      if (error)
        return error;
      op->resource = 0;
      op->flags |= SYSOP_RESOURCE_MOVED;
    }
    error = sysopsaved(work, cache, key);
    if (!error)
      error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  return sysopfinish(work, cache, key, file);
}
isize
sysoprecover(Connwork *work, Conncache *cache, const Opkey *key, u64 *file)
{
  Slotop   *op;
  Connentry entry;
  u32       i;
  isize     error;
  if (!output(work, cache, file, sizeof(*file)) || *file ||
      !separate(key, sizeof(*key), file, sizeof(*file)))
    return -ERR_INVALID;
  error = match(work, cache, key, &op);
  if (error)
    return error;
  error = sysconnview(cache, work->state.tag, &entry);
  if (error)
    return error;
  if (work->state.ended != 1 || entry.state != SYSCONN_DRAINING ||
      entry.peer.pid != work->state.peer.pid || entry.peer.image != work->state.peer.image)
    return -ERR_STATE;
  /* A lost PATH reply cannot tell us whether a namespace change committed. */
  if ((op->opcode != FS_READ && op->opcode != FS_WRITE && op->opcode != FS_SEEK &&
       op->opcode != FS_TTYCTL && op->opcode != FS_STAT && op->opcode != FS_SYNC) ||
      op->flags & SYSOP_CREATOR)
    return -ERR_NOTSUP;
  if (op->phase == SYSOP_PREPARED)
    return sysopabort(work, cache, key, file);
  if (op->phase == SYSOP_WAITING) {
    error = sysoppoll(work, cache, key);
    if (error)
      return error;
  }
  if ((op->phase != SYSOP_RESULT && op->phase != SYSOP_ACKED) || op->status != -ERR_PEER ||
      op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT)
    return -ERR_STATE;
  for (i = 0; i < 4; i++)
    if (op->words[i])
      return -ERR_STATE;
  if (op->phase == SYSOP_RESULT) {
    error = sysopfinished(work, cache, key);
    if (!error)
      error = sysopsaved(work, cache, key);
    if (!error)
      error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  return sysopfinish(work, cache, key, file);
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
sysopabandon(Connwork *work, Conncache *cache, Opkey *owned_key, Opkey *out)
{
  Slotop *op;
  isize   error;
  if (!output(work, cache, owned_key, sizeof(*owned_key)) ||
      !output(work, cache, out, sizeof(*out)) ||
      !separate(owned_key, sizeof(*owned_key), out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(work, cache, owned_key, &op);
  if (error)
    return error;
  if (op->flags & SYSOP_CREATOR &&
      work->state.creators[owned_key->lease.slot].phase == SYSFILE_CREATE_RESOLVED &&
      !work->state.creators[owned_key->lease.slot].disposition)
    return -ERR_STATE;
  error = sysopbackground(work, cache, owned_key, out);
  if (error)
    return error;
  work->state.operations[out->lease.slot].flags |= SYSOP_APP_ENDED;
  return 0;
}
isize
sysopsaved(Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = match(work, cache, key, &op);
  if (error)
    return error;
  if (op->phase != SYSOP_RESULT ||
      (op->flags & SYSOP_CREATOR &&
       work->state.creators[key->lease.slot].phase < SYSFILE_CREATE_CAPTURED))
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
  if (op->phase != phase || op->rpc || op->resource || op->flags & SYSOP_CREATOR ||
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

isize
sysloadstage(Connwork *work, Conncache *cache, u32 layer, u64 owner, u64 session,
             const u64 identity[4], u64 offset, u64 bytes, Loadrequest *load)
{
  Fileslot slot;
  u64      file = 0, address;
  u32      i;
  isize    error;
  if (!output(work, cache, load, sizeof(*load)) || !identity || !session || !identity[0] ||
      !identity[1] || !identity[2] || !bytes || bytes > FS_SLOT_BYTES || offset > identity[3] ||
      bytes > identity[3] - offset)
    return -ERR_INVALID;
  if (load->key.lease.seq)
    return -ERR_BUSY;
  for (i = 0; i < sizeof(slot); i++)
    ((volatile u8 *) &slot)[i] = 0;
  for (i = 0; i < 4; i++)
    slot.args[i] = identity[i];
  error = sysopprepare(work, cache, layer, 1, owner, &file, session, FS_LOAD, 0, &load->key);
  if (error)
    return error;
  slot.seq = load->key.lease.seq;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = session;
  slot.payload_bytes = bytes;
  slot.args[4] = offset;
  slot.args[5] = bytes;
  address = work->state.map.address + FS_DESC_OFFSET + load->key.lease.slot * FS_DESC_BYTES;
  error = __sys_copy((void *) (uintptr_t) address, &slot, sizeof(slot));
  if (error) {
    if (sysopabort(work, cache, &load->key, &file))
      return -ERR_STATE;
    load->key.lease.seq = 0;
    return error;
  }
  load->bytes = bytes;
  load->native = load->business = 0;
  return 0;
}

isize
sysloadcollect(Connwork *work, Conncache *cache, Loadrequest *load, void *destination)
{
  Slotop *op;
  u64     file;
  Handle  dropped;
  isize   error;
  if (!output(work, cache, load, sizeof(*load)) || !load->bytes || load->bytes > FS_SLOT_BYTES ||
      (destination && !separate(load, sizeof(*load), destination, load->bytes)))
    return -ERR_INVALID;
  error = match(work, cache, &load->key, &op);
  if (error)
    return error;
  if (op->opcode != FS_LOAD || op->file || load->key.lease.span != 1)
    return -ERR_INVALID;
  error = sysoppoll(work, cache, &load->key);
  if (!error)
    error = sysopfinished(work, cache, &load->key);
  if (error)
    return error;
  if (!(op->flags & SYSOP_SAVED)) {
    if (op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT)
      load->native = -ERR_STATE;
    else if (op->status)
      load->native =
          op->words[0] || op->words[1] || op->words[2] || op->words[3] ? -ERR_STATE : op->status;
    else if (op->words[1] != load->key.lease.seq || (isize) op->words[0] > 0 ||
             (op->words[0] ? op->words[2] || op->words[3]
                           : op->words[2] != load->bytes || op->words[3] != load->bytes))
      load->native = -ERR_STATE;
    else if (op->words[0])
      load->business = (isize) op->words[0];
    else if (destination)
      load->native = sysopcopy(work, cache, &load->key, 0, load->bytes, destination);
    if (op->resource) {
      error = __sys_free(op->resource);
      if (!error)
        error = sysopresource(work, cache, &load->key, &dropped);
      if (error)
        return error;
    }
    error = sysopsaved(work, cache, &load->key);
    if (error)
      return error;
  }
  if (op->phase != SYSOP_ACKED) {
    error = sysopack(work, cache, &load->key);
    if (error)
      return error;
  }
  error = sysopfinish(work, cache, &load->key, &file);
  if (!error)
    load->key.lease.seq = 0;
  return error;
}
