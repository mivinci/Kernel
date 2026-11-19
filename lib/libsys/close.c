#include "private/close.h"
#include "private/syscall.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static isize
anchorcheck(Filepool *pool, Connwork *work, const Fileclose *anchor, Fileref *out)
{
  isize error;
  if (!pool || !work || !anchor || !separate(pool, sizeof(*pool), anchor, sizeof(*anchor)) ||
      !separate(work, sizeof(*work), anchor, sizeof(*anchor)))
    return -ERR_INVALID;
  error = sysfilerefview(pool, work->state.self, anchor->file, out);
  if (error)
    return error;
  if (!anchor->sequence || out->state != SYSFILE_REF_CLOSING || out->aliases || out->pins ||
      out->close_sequence != anchor->sequence ||
      out->connection.generation != work->state.tag.generation ||
      out->connection.index != work->state.tag.index)
    return -ERR_STALE;
  return 0;
}
isize
sysfilecloseprepare(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                    u64 owner, u64 deadline, Opkey *key, Msg *message)
{
  Fileref  reference;
  Fileslot descriptor;
  Opkey    prepared;
  u64      file;
  u32      i;
  isize    error = anchorcheck(pool, work, anchor, &reference);
  if (error)
    return error;
  if (!separate(work, sizeof(*work), key, sizeof(*key)) ||
      !separate(cache, sizeof(*cache), key, sizeof(*key)) ||
      !separate(pool, sizeof(*pool), key, sizeof(*key)) ||
      !separate(work, sizeof(*work), message, sizeof(*message)) ||
      !separate(cache, sizeof(*cache), message, sizeof(*message)) ||
      !separate(pool, sizeof(*pool), message, sizeof(*message)) ||
      !separate(key, sizeof(*key), message, sizeof(*message)) ||
      !separate(anchor, sizeof(*anchor), key, sizeof(*key)) ||
      !separate(anchor, sizeof(*anchor), message, sizeof(*message)))
    return -ERR_INVALID;
  file = anchor->file;
  error = sysopprepare(work, cache, SYSFILE_CONTROL, 1, owner, &file, reference.session, FS_CLOSE,
                       deadline, &prepared);
  if (error)
    return error;
  for (i = 0; i < sizeof(descriptor); i++)
    ((u8 *) &descriptor)[i] = 0;
  descriptor.seq = prepared.lease.seq;
  descriptor.state = FS_SLOT_SUBMITTED;
  descriptor.object = reference.session;
  descriptor.span = 1;
  descriptor.args[0] = reference.reference;
  error = __sys_copy((void *) (uintptr_t) (work->state.map.address + FS_DESC_OFFSET +
                                           prepared.lease.slot * FS_DESC_BYTES),
                     &descriptor, sizeof(descriptor));
  if (error) {
    if (sysopabort(work, cache, &prepared, &file))
      return -ERR_STATE;
    return error;
  }
  key->connection.generation = prepared.connection.generation;
  key->connection.index = prepared.connection.index;
  key->connection.reserved = 0;
  key->lease.seq = prepared.lease.seq;
  key->lease.slot = prepared.lease.slot;
  key->lease.span = prepared.lease.span;
  key->owner = prepared.owner;
  message->words[0] = FS_CLOSE;
  message->words[1] = work->state.map.connid;
  message->words[2] = prepared.lease.seq;
  message->words[3] = prepared.lease.slot;
  return 0;
}
isize
sysfilecloseresult(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                   const Opkey *key)
{
  Fileref reference;
  Slotop *op;
  isize   error = anchorcheck(pool, work, anchor, &reference);
  if (error)
    return error;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if ((op->phase != SYSOP_RESULT && op->phase != SYSOP_ACKED) || op->opcode != FS_CLOSE ||
      key->lease.slot != 6 || key->lease.span != 1 || op->file != anchor->file ||
      op->object != reference.session)
    return -ERR_STATE;
  if (op->status)
    return op->status;
  if (op->flags & SYSOP_BAD_RESULT || op->kind != IPC_NONE || op->resource || op->words[0] ||
      op->words[1] != key->lease.seq || op->words[2] || op->words[3])
    return -ERR_INVALID;
  return 0;
}
isize
sysfileclosecomplete(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                     const Opkey *key)
{
  u64   file;
  isize error = sysfilecloseresult(pool, work, cache, anchor, key);
  if (error)
    return error;
  if (work->state.operations[key->lease.slot].phase != SYSOP_ACKED)
    return -ERR_BUSY;
  error = sysopfinish(work, cache, key, &file);
  if (error)
    return error;
  return sysfilerefconfirmed(pool, work->state.self, anchor);
}
