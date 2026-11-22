#include "blk.h"
#include "private/bootstrap.h"
#include "private/file.h"
#include <errno.h>

static void
copy(volatile void *out, const volatile void *in, u32 bytes)
{
  volatile u8       *destination = out;
  const volatile u8 *source = in;
  u32                i;
  for (i = 0; i < bytes; i++)
    destination[i] = source[i];
}

static volatile Protoslot *
slot(const Fsblk *blk)
{
  return (volatile Protoslot *) (uintptr_t) (blk->address + PROTO_DESC_OFFSET);
}

static volatile u8 *
payload(const Fsblk *blk)
{
  return (volatile u8 *) (uintptr_t) (blk->address + PROTO_DATA_OFFSET);
}

isize
fsblkbind(Fsblk *blk, Handle call, const Bootmap *map, u64 device)
{
  Filehead head;
  isize    error;
  if (!blk || blk->address || call <= 0 || !map || !map->mapped || !map->verified ||
      !map->address || (map->address & PAGE_MASK) || map->address > UINT64_MAX - PROTO_CONN_BYTES ||
      map->bytes != PROTO_CONN_BYTES || !map->connid || !map->serverimage || !device)
    return -ERR_INVALID;
  error = sysfilehead((const volatile void *) (uintptr_t) map->address, map->bytes, map->connid,
                      map->serverimage, &head);
  if (error)
    return error;
  blk->call = call;
  blk->address = map->address;
  blk->connid = map->connid;
  blk->device = device;
  blk->seq = 1;
  return 0;
}

isize
fsblksubmit(Fsblk *blk, u32 opcode, u64 offset, u32 bytes, const void *write)
{
  Protoslot entry = {0};
  Ipcmsg    result = {0};
  Msg       msg;
  isize     error;
  if (!blk || !blk->address || opcode < BLK_INFO || opcode > BLK_FLUSH)
    return -ERR_INVALID;
  if (blk->rpc)
    return -ERR_BUSY;
  if (blk->ended)
    return -ERR_PEER;
  if (blk->seq == UINT64_MAX)
    return -ERR_OVERFLOW;
  if (opcode == BLK_INFO || opcode == BLK_FLUSH) {
    if (offset || bytes || write)
      return -ERR_INVALID;
  } else if (!bytes || bytes > BLK_MAX_IO || (opcode == BLK_WRITE) != (write != NULL))
    return -ERR_INVALID;
  if (opcode != BLK_INFO) {
    const Blkinfo *info = &blk->info;
    if (blk->stale || !info->geometry_generation)
      return -ERR_STALE;
    if ((opcode == BLK_WRITE && (info->flags & BLK_READONLY)) ||
        (opcode == BLK_FLUSH && !(info->flags & BLK_FLUSHABLE)))
      return -ERR_NOTSUP;
    if (offset > info->capacity_bytes || bytes > info->capacity_bytes - offset ||
        offset % info->logical_bytes || bytes % info->logical_bytes)
      return -ERR_INVALID;
    entry.args[0] = offset;
    entry.args[1] = bytes;
    entry.args[2] = info->geometry_generation;
  }
  entry.seq = ++blk->seq;
  entry.payload_bytes = opcode == BLK_INFO ? BLK_INFO_BYTES : bytes;
  entry.span = 1;
  if (write)
    copy(payload(blk), write, bytes);
  copy(slot(blk), &entry, sizeof(entry));
  __atomic_store_n(&slot(blk)->state, PROTO_SLOT_SUBMITTED, __ATOMIC_RELEASE);
  msg = (Msg){{BLK_WORD(opcode), blk->connid, entry.seq, 0}};
  error = ipc_call(blk->call, &msg, CALL_ASYNC, &result);
  if (handlekind(result.request) != HANDLE_RPC)
    return error ? error : -ERR_STATE;
  blk->rpc = result.request;
  blk->opcode = opcode;
  blk->bytes = (u32) entry.payload_bytes;
  return 0;
}

static int
validinfo(const Fsblk *blk, u64 geometry)
{
  const Blkinfo *info = &blk->nextinfo;
  u64            logical = info->logical_bytes, physical = info->physical_bytes;
  return info->magic == BLK_INFO_MAGIC && info->version == BLK_VERSION &&
         info->device_generation == blk->device && geometry &&
         info->geometry_generation == geometry && geometry >= blk->info.geometry_generation &&
         (!blk->stale || geometry > blk->info.geometry_generation) && logical >= 512 &&
         !(logical & (logical - 1)) && !(info->capacity_bytes % 512) &&
         (physical ? physical >= logical && !(physical & (physical - 1)) &&
                         info->alignment_bytes < physical && !(info->alignment_bytes % logical)
                   : !info->alignment_bytes) &&
         !(info->flags & ~(BLK_READONLY | BLK_FLUSHABLE)) && !info->reserved[0] &&
         !info->reserved[1] && !info->reserved[2];
}

/* Only a validated negative business reply can report retryable
   backpressure. A native failure or corrupt reply has no such guarantee. */
static isize
capture(Fsblk *blk, const Ipcmsg *msg, isize native)
{
  isize status = (isize) msg->words[0];
  if (native == -ERR_CANCELLED)
    return -EINTR;
  if (native || msg->kind != IPC_NONE || msg->source || msg->request || msg->image ||
      msg->words[1] != blk->seq || status > 0)
    return -EIO;
  if (status)
    return msg->words[2] || msg->words[3] ? -EIO : status;
  if (__atomic_load_n(&slot(blk)->state, __ATOMIC_ACQUIRE) != PROTO_SLOT_DONE ||
      slot(blk)->seq != blk->seq || msg->words[3] != blk->bytes)
    return -EIO;
  if (blk->opcode == BLK_INFO) {
    copy(&blk->nextinfo, payload(blk), BLK_INFO_BYTES);
    if (!validinfo(blk, msg->words[2]))
      return -EIO;
  } else if (msg->words[2] != blk->bytes)
    return -EIO;
  if (blk->opcode == BLK_READ)
    copy(blk->data, payload(blk), blk->bytes);
  return 0;
}

isize
fsblkpoll(Fsblk *blk, void *read, u32 capacity, isize *status)
{
  Ipcmsg result;
  isize  error;
  if (!blk || !blk->rpc || !status ||
      (blk->opcode == BLK_READ ? !read || capacity < blk->bytes : read || capacity))
    return -ERR_INVALID;
  if (!blk->saved) {
    error = ipc_rqpoll(blk->rpc, RQ_FINISH, &result);
    if (error)
      return error;
    error = ipc_rqpoll(blk->rpc, 0, &result);
    if (error && error != -ERR_CANCELLED && error != -ERR_PEER && error != -ERR_REVOKED)
      return error;
    blk->result = capture(blk, &result, error);
    blk->ended = error == -ERR_PEER || error == -ERR_REVOKED;
    blk->saved = 1;
  }
  error = ipc_rqack(blk->rpc, ACK_RESULT | ACK_RELEASE);
  if (error)
    return error;
  if (!blk->result) {
    if (blk->opcode == BLK_READ)
      copy(read, blk->data, blk->bytes);
    if (blk->opcode == BLK_INFO) {
      blk->info = blk->nextinfo;
      blk->stale = 0;
    }
  } else if (blk->result == -ESTALE && blk->opcode != BLK_INFO)
    blk->stale = 1;
  *status = blk->result;
  blk->rpc = 0;
  blk->saved = 0;
  return 0;
}
