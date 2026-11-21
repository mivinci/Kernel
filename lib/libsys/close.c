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
static isize
target(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor, const Opkey *key,
       Slotop **out)
{
  Fileref reference;
  Slotop *op;
  isize   error = anchorcheck(pool, work, anchor, &reference);
  if (!error)
    error = sysopcheck(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (op->phase == SYSOP_PREPARED || op->opcode != FS_CLOSE || key->lease.slot != 6 ||
      key->lease.span != 1 || op->file != anchor->file || op->object != reference.session)
    return -ERR_STATE;
  *out = op;
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
sysfileclosepick(Filepool *pool, Connwork *work, Conncache *cache, u64 owner, u64 deadline,
                 Fileclose *anchor, Opkey *key, Msg *message)
{
  Fileclose claimed;
  u64       file;
  isize     error;
  if (!separate(pool, sizeof(*pool), anchor, sizeof(*anchor)) ||
      !separate(work, sizeof(*work), anchor, sizeof(*anchor)) ||
      !separate(cache, sizeof(*cache), anchor, sizeof(*anchor)) ||
      !separate(key, sizeof(*key), anchor, sizeof(*anchor)) ||
      !separate(message, sizeof(*message), anchor, sizeof(*anchor)))
    return -ERR_INVALID;
  error = sysfilerefnextclose(pool, work->state.self, work->state.tag, 0, &file);
  if (!error)
    error = sysfilerefclose(pool, work->state.self, file, &claimed);
  if (error)
    return error;
  error = sysfilecloseprepare(pool, work, cache, &claimed, owner, deadline, key, message);
  if (error) {
    /* A failed preparation normally owns no slot. Keep the claim if an
       internal rollback failed and left responsibility attached to it. */
    if (work->state.operations[6].phase && work->state.operations[6].file == file)
      return error;
    if (sysfilerefunclaimclose(pool, work->state.self, &claimed))
      return -ERR_STATE;
    return error;
  }
  *anchor = claimed;
  return 0;
}

isize
sysfilecloseresult(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                   const Opkey *key)
{
  Slotop *op;
  isize   error = target(pool, work, cache, anchor, key, &op);
  if (error)
    return error;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  if (op->phase != SYSOP_RESULT && op->phase != SYSOP_ACKED)
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
static isize
progress(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
         const Opkey *key, u32 *confirmed)
{
  isize error;
  *confirmed = 0;
  error = sysfilecloseresult(pool, work, cache, anchor, key);
  if (error)
    return error;
  *confirmed = 1;
  error = sysopfinished(work, cache, key);
  if (error)
    return error;
  if (work->state.operations[key->lease.slot].phase == SYSOP_RESULT) {
    error = sysopsaved(work, cache, key);
    if (!error)
      error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  return sysfileclosecomplete(pool, work, cache, anchor, key);
}
static isize
step(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor, Opkey *owned_key,
     u64 now, u32 interrupted, Opkey *out)
{
  Slotop *op;
  u32     confirmed, i;
  isize   error, outcome;
  if (!separate(pool, sizeof(*pool), owned_key, sizeof(*owned_key)) ||
      !separate(work, sizeof(*work), owned_key, sizeof(*owned_key)) ||
      !separate(cache, sizeof(*cache), owned_key, sizeof(*owned_key)) ||
      !separate(anchor, sizeof(*anchor), owned_key, sizeof(*owned_key)) ||
      !separate(pool, sizeof(*pool), out, sizeof(*out)) ||
      !separate(work, sizeof(*work), out, sizeof(*out)) ||
      !separate(cache, sizeof(*cache), out, sizeof(*out)) ||
      !separate(anchor, sizeof(*anchor), out, sizeof(*out)) ||
      !separate(owned_key, sizeof(*owned_key), out, sizeof(*out)))
    return -ERR_INVALID;
  error = target(pool, work, cache, anchor, owned_key, &op);
  if (error)
    return error;
  if (op->flags & SYSOP_BACKGROUND)
    return -ERR_STATE;
  error = progress(pool, work, cache, anchor, owned_key, &confirmed);
  if (!error) {
    for (i = 0; i < sizeof(*owned_key); i++)
      ((u8 *) owned_key)[i] = 0;
    return 0;
  }
  outcome = SYSFILE_CLOSE_FAULT;
  if (confirmed)
    outcome = SYSFILE_CLOSE_CONFIRMED;
  else if (error == -ERR_WOULD_BLOCK) {
    if (interrupted)
      outcome = SYSFILE_CLOSE_SIGNAL;
    else {
      if (!op->deadline)
        return -ERR_STATE;
      if (now < op->deadline)
        return -ERR_WOULD_BLOCK;
    }
  }
  error = sysopabandon(work, cache, owned_key, out);
  return error ? error : outcome;
}
isize
sysfileclosewatch(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                  Opkey *owned_key, u64 now, Opkey *out)
{
  return step(pool, work, cache, anchor, owned_key, now, 0, out);
}
isize
sysfilecloseinterrupt(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                      Opkey *owned_key, Opkey *out)
{
  return step(pool, work, cache, anchor, owned_key, 0, 1, out);
}
isize
sysfileclosework(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                 const Opkey *key)
{
  Slotop *op;
  u32     confirmed;
  isize   error = target(pool, work, cache, anchor, key, &op);
  if (error)
    return error;
  if ((op->flags & (SYSOP_BACKGROUND | SYSOP_APP_ENDED)) != (SYSOP_BACKGROUND | SYSOP_APP_ENDED))
    return -ERR_STATE;
  return progress(pool, work, cache, anchor, key, &confirmed);
}
isize
sysfileclosepump(Filepool *pool, Connwork *work, Conncache *cache, u64 owner, u64 deadline)
{
  Fileclose anchor;
  Fileref   reference;
  Slotop   *op;
  Opkey     key, background;
  Msg       message;
  Ipcmsg    result;
  u64       file = 0;
  isize     error;
  if (!pool || !work || !cache)
    return -ERR_INVALID;
  op = &work->state.operations[6];
  if (op->phase) {
    if (op->opcode != FS_CLOSE ||
        (op->flags & (SYSOP_BACKGROUND | SYSOP_APP_ENDED)) != (SYSOP_BACKGROUND | SYSOP_APP_ENDED))
      return -ERR_BUSY;
    error = sysfilerefview(pool, work->state.self, op->file, &reference);
    if (error)
      return error;
    anchor.file = op->file;
    anchor.sequence = reference.close_sequence;
    key.connection = work->state.tag;
    key.lease = op->lease;
    key.owner = 0;
    return sysfileclosework(pool, work, cache, &anchor, &key);
  }
  error = sysfileclosepick(pool, work, cache, owner, deadline, &anchor, &key, &message);
  if (error)
    return error;
  error = ipc_call(work->state.call, &message, SYSFILE_CLOSE_OPTIONS, &result);
  if (error) {
    if (sysopabort(work, cache, &key, &file) || file != anchor.file ||
        sysfilerefunclaimclose(pool, work->state.self, &anchor))
      return -ERR_STATE;
    return error;
  }
  error = sysopsubmitted(work, cache, &key, &result.request);
  if (!error)
    error = sysopabandon(work, cache, &key, &background);
  return error ? error : SYSFILE_CLOSE_SUBMITTED;
}
static isize
peerended(Connwork *work, Conncache *cache)
{
  Connentry entry;
  isize     error;
  if (!work || !cache || work->state.ended != 1)
    return -ERR_STATE;
  error = sysconncheck(cache, work->state.self);
  if (!error)
    error = sysconnview(cache, work->state.tag, &entry);
  if (error)
    return error;
  if (entry.state != SYSCONN_DRAINING || entry.peer.pid != work->state.peer.pid ||
      entry.peer.image != work->state.peer.image)
    return -ERR_STALE;
  return 0;
}
isize
sysfilecloseended(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor)
{
  Fileref reference;
  u32     i;
  isize   error = anchorcheck(pool, work, anchor, &reference);
  if (!error)
    error = peerended(work, cache);
  if (error)
    return error;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (work->state.operations[i].phase && work->state.operations[i].file == anchor->file)
      return -ERR_BUSY;
  return sysfilerefconfirmed(pool, work->state.self, anchor);
}
isize
sysfilecloserecover(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                    const Opkey *key)
{
  Slotop *op;
  u64     file = 0;
  isize   error = target(pool, work, cache, anchor, key, &op);
  if (!error)
    error = peerended(work, cache);
  if (error)
    return error;
  if (op->phase == SYSOP_WAITING) {
    error = sysoppoll(work, cache, key);
    if (error)
      return error;
  }
  if (op->phase == SYSOP_RESULT) {
    error = sysopfinished(work, cache, key);
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
  error = sysopfinish(work, cache, key, &file);
  if (error)
    return error;
  return sysfilerefconfirmed(pool, work->state.self, anchor);
}
