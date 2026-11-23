#include "private/execsend.h"
#include "private/contextipc.h"
#include "private/syscall.h"

static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}

isize
sysforkfdnext(const Execfds *capture, u32 *cursor, Connpeer *peer, u64 *file)
{
  u32 i, j;
  if (!capture || !cursor || !peer || !file)
    return -ERR_INVALID;
  if (!capture->pool || capture->ready != 1 || capture->count > BOOT_FD_COUNT ||
      *cursor > capture->count)
    return -ERR_STATE;
  for (i = *cursor; i < capture->count; i++) {
    Connpeer candidate = capture->entries[i].backend;
    for (j = 0; j < i; j++)
      if (capture->entries[j].backend.pid == candidate.pid &&
          capture->entries[j].backend.image == candidate.image)
        break;
    if (j != i)
      continue;
    *peer = candidate;
    *file = capture->entries[i].file;
    *cursor = i + 1;
    return 1;
  }
  return 0;
}

static isize
fdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context, u64 owner,
        Connpeer target, u64 token, u64 deadline, u64 *owned_file, Execsend *send, u32 opcode)
{
  Fileslot slot;
  Fileref  pin;
  Opkey   *key;
  u64      address, header, i, continuation[FS_DESC_BYTES / sizeof(u64)];
  isize    count, error;
  if (!capture || !work || !cache || !capture->pool || context > SYSFILE_SIGNAL2 ||
      !separate(send, sizeof(*send), work, sizeof(*work)) ||
      !separate(send, sizeof(*send), cache, sizeof(*cache)) ||
      !separate(send, sizeof(*send), capture, sizeof(*capture)) ||
      !separate(send, sizeof(*send), capture->pool, sizeof(*capture->pool)) ||
      !separate(owned_file, sizeof(*owned_file), send, sizeof(*send)) ||
      !separate(owned_file, sizeof(*owned_file), capture, sizeof(*capture)) ||
      !separate(owned_file, sizeof(*owned_file), capture->pool, sizeof(*capture->pool)))
    return -ERR_INVALID;
  if (capture->self.pid != work->state.self.pid || capture->self.image != work->state.self.image)
    return -ERR_STALE;
  if (send->key.lease.seq)
    return -ERR_BUSY;
  if (opcode == FS_EXEC_PREPARE)
    count = sysexecfdmanifest(capture, work->state.peer, target.image, deadline, slot.args,
                              send->rows, BOOT_FD_COUNT);
  else
    count = sysforkfdmanifest(capture, work->state.peer, target, token, deadline, slot.args,
                              send->rows, BOOT_FD_COUNT);
  if (count <= 0)
    return count < 0 ? count : -ERR_STALE;
  error = sysfilerefview(capture->pool, capture->self, *owned_file, &pin);
  if (error)
    return error;
  if (!pin.pins || pin.connection.index != work->state.tag.index ||
      pin.connection.generation != work->state.tag.generation)
    return -ERR_STATE;
  slot.payload_bytes = (u64) count * sizeof(Fileinherit);
  slot.span = slot.payload_bytes > FS_SLOT_BYTES ? 2 : 1;
  slot.object = slot.reserved[0] = slot.reserved[1] = slot.reserved[2] = 0;
  key = &send->key;
  error = sysopprepare(work, cache, context, (u32) slot.span, owner, owned_file, 0, opcode,
                       deadline, key);
  if (error)
    return error;
  slot.seq = key->lease.seq;
  slot.state = FS_SLOT_SUBMITTED;
  address = work->state.map.address;
  header = address + FS_DESC_OFFSET + key->lease.slot * FS_DESC_BYTES;
  if (slot.span == 2) {
    for (i = 0; i < ARRAY_SIZE(continuation); i++)
      continuation[i] = 0;
    error = __sys_copy((void *) (header + FS_DESC_BYTES), continuation, sizeof(continuation));
  }
  if (!error)
    error = __sys_copy((void *) (address + FS_DATA_OFFSET + key->lease.slot * FS_SLOT_BYTES),
                       send->rows, slot.payload_bytes);
  if (!error)
    error = __sys_copy((void *) header, &slot, sizeof(slot));
  if (error) {
    if (sysopabort(work, cache, key, owned_file))
      return -ERR_STATE;
    key->lease.seq = 0;
    return error;
  }
  send->business = 0;
  send->snapshot = 0;
  return 0;
}

isize
sysexecfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context, u64 owner,
               u64 target_image, u64 deadline, u64 *owned_file, Execsend *send)
{
  Connpeer target = {0, target_image};
  return fdbegin(capture, work, cache, context, owner, target, 0, deadline, owned_file, send,
                 FS_EXEC_PREPARE);
}

isize
sysforkfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context, u64 owner,
               Connpeer child, u64 token, u64 deadline, u64 *owned_file, Execsend *send)
{
  return fdbegin(capture, work, cache, context, owner, child, token, deadline, owned_file, send,
                 token ? FS_FORK_PREPARE : FS_FORK_SNAPSHOT);
}

isize
sysforkfdcollect(Connwork *work, Conncache *cache, Execsend *send, u64 *owned_file)
{
  const Slotop *op;
  isize         error;
  if (!separate(send, sizeof(*send), work, sizeof(*work)) ||
      !separate(send, sizeof(*send), cache, sizeof(*cache)) ||
      !separate(owned_file, sizeof(*owned_file), send, sizeof(*send)) ||
      !separate(owned_file, sizeof(*owned_file), work, sizeof(*work)) ||
      !separate(owned_file, sizeof(*owned_file), cache, sizeof(*cache)) || *owned_file)
    return -ERR_INVALID;
  error = sysopcheck(work, cache, &send->key);
  if (error)
    return error;
  op = &work->state.operations[send->key.lease.slot];
  if (op->opcode != FS_FORK_SNAPSHOT && op->opcode != FS_FORK_PREPARE)
    return -ERR_INVALID;
  if (op->phase == SYSOP_WAITING) {
    error = sysoppoll(work, cache, &send->key);
    if (error)
      return error;
  }
  if (op->phase == SYSOP_RESULT) {
    error = sysopfinished(work, cache, &send->key);
    if (error)
      return error;
    if (op->status || op->words[1] != send->key.lease.seq || op->words[3] ||
        (isize) op->words[0] > 0 ||
        (!!op->words[2] != (!op->words[0] && op->opcode == FS_FORK_SNAPSHOT)))
      return op->status ? op->status : -ERR_STATE;
    send->business = (isize) op->words[0];
    send->snapshot = op->words[2];
    error = sysopsaved(work, cache, &send->key);
    if (error)
      return error;
  }
  if (op->phase != SYSOP_ACKED) {
    error = sysopack(work, cache, &send->key);
    if (error)
      return error;
  }
  error = sysopfinish(work, cache, &send->key, owned_file);
  if (!error)
    send->key.lease.seq = 0;
  return error;
}

isize
sysforkfddrive(Rbank *bank, u32 layer, Connwork *work, Conncache *cache, Execsend *send,
                u64 *owned_file)
{
  Rkey    active;
  Slotop *op;
  isize   error, status;
  if (!bank || !work || !cache || !send || !owned_file || *owned_file)
    return -ERR_INVALID;
  error = sysopcheck(work, cache, &send->key);
  if (error)
    return error;
  op = &work->state.operations[send->key.lease.slot];
  if (op->opcode != FS_FORK_SNAPSHOT && op->opcode != FS_FORK_PREPARE)
    return -ERR_INVALID;
  while (op->phase == SYSOP_PREPARED || op->phase == SYSOP_WAITING) {
    if (op->phase == SYSOP_PREPARED)
      error = sysctxopcall(bank, cache->self, layer, work, cache, &send->key, 0, &active);
    else
      error = sysctxopwait(bank, cache->self, layer, work, cache, &send->key, &active);
    if (error)
      return error;
    status = sysctxinvoke(bank, cache->self, active);
    error = sysctxhandoff(bank, cache->self, active);
    if (!error)
      error = sysctxfinish(bank, cache->self, active);
    if (error || status)
      return error ? error : status;
  }
  return sysforkfdcollect(work, cache, send, owned_file);
}

isize
sysforkfdprepare(Forksend *tx, const Execfds *capture, Connwork *works, Conncache *cache,
                 Rbank *bank, u32 layer, Connpeer child, u64 deadline)
{
  Connpeer peer;
  Connwork *work;
  Fileref ref;
  isize error;
  if (!tx || !capture || !works || !cache || !bank || layer >= RCTX_LAYERS ||
      tx->count > FIX_CONN_COUNT || !pidvalid(child.pid) || !child.image)
    return -ERR_INVALID;
  if (tx->child.pid && (tx->child.pid != child.pid || tx->child.image != child.image))
    return -ERR_STALE;
  tx->child = child;
  for (;;) {
    if (!tx->representative) {
      error = sysforkfdnext(capture, &tx->cursor, &peer, &tx->representative);
      if (error <= 0) {
        if (!error)
          tx->ready = 1;
        return error;
      }
    }
    if (tx->count == FIX_CONN_COUNT)
      return -ERR_CAPACITY;
    error = sysfilerefview(capture->pool, capture->self, tx->representative, &ref);
    if (error)
      return error;
    work = &works[ref.connection.index];
    if (!tx->send.key.lease.seq) {
      if (bank->sequence == UINT64_MAX)
        return -ERR_OVERFLOW;
      if (!tx->pin) {
        error = sysfilerefpin(capture->pool, capture->self, tx->representative);
        if (error)
          return error;
        tx->pin = tx->representative;
      }
      error = sysforkfdbegin(capture, work, cache, layer, bank->sequence + 1, child,
                             tx->snapshot, deadline, &tx->pin, &tx->send);
      if (error)
        return error;
    }
    error = sysforkfddrive(bank, layer, work, cache, &tx->send, &tx->pin);
    if (error)
      return error;
    error = sysfilerefunpin(capture->pool, capture->self, tx->pin);
    if (error)
      return error;
    tx->pin = 0;
    if (tx->send.business)
      return tx->send.business;
    if (!tx->snapshot) {
      tx->snapshot = tx->send.snapshot;
      continue;
    }
    tx->peers[tx->count++] = (Fixconn){work->state.peer.pid, work->state.peer.image, tx->snapshot};
    tx->representative = tx->snapshot = 0;
  }
}

isize
sysforkfdabort(Forksend *tx, Execfds *capture, Connwork *works, Conncache *cache,
               Elfwork *child)
{
  Connwork *work;
  Opkey    *key;
  isize     error;
  if (!tx || !capture || !works || !cache || !child)
    return -ERR_INVALID;
  if (tx->child.pid && tx->child.pid != child->pid)
    return -ERR_STALE;
  tx->ready = 0;
  error = sysforkcancel(child);
  if (error)
    return error;
  key = &tx->send.key;
  if (key->lease.seq) {
    if (key->connection.index >= SYSCONN_COUNT || tx->pin)
      return -ERR_STATE;
    work = &works[key->connection.index];
    error = sysopcheck(work, cache, key);
    if (error)
      return error;
    if (work->state.operations[key->lease.slot].phase == SYSOP_PREPARED) {
      error = sysopabort(work, cache, key, &tx->pin);
      if (!error)
        key->lease.seq = 0;
    } else
      error = sysforkfdcollect(work, cache, &tx->send, &tx->pin);
    if (error)
      return error;
  }
  if (tx->pin) {
    error = sysfilerefunpin(capture->pool, capture->self, tx->pin);
    if (error)
      return error;
    tx->pin = 0;
  }
  return sysexecfdrelease(capture);
}

isize
sysforkfdpublish(Forksend *tx, Elfwork *child, Rbank *bank, Connpeer parent, const Forkfix *fix)
{
  Forkfix plan;
  if (!tx || !child || !bank || !fix || fix->connections || fix->connection_count)
    return -ERR_INVALID;
  if (tx->child.pid != child->pid)
    return -ERR_STALE;
  if (tx->ready != 1 || tx->count > FIX_CONN_COUNT || tx->representative || tx->snapshot ||
      tx->pin || tx->send.key.lease.seq)
    return -ERR_STATE;
  plan.address = fix->address;
  plan.capacity = fix->capacity;
  plan.buffer = fix->buffer;
  plan.resources = fix->resources;
  plan.resource_count = fix->resource_count;
  plan.connections = tx->peers;
  plan.connection_count = tx->count;
  tx->ready = 0;
  return sysforkfinish(child, bank, parent, &plan);
}
