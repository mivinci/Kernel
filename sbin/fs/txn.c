#include "txn.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static int
external(const Txn *txn, const void *out, u64 bytes)
{
  Cache *cache;
  if (!txn || !(cache = txn->cache) || !cache->entries || !cache->capacity ||
      cache->capacity > CACHE_MAX)
    return 0;
  return separate(txn, sizeof(*txn), out, bytes) && separate(cache, sizeof(*cache), out, bytes) &&
         separate(cache->entries, (u64) cache->capacity * sizeof(*cache->entries), out, bytes);
}
isize
txninit(Txn *txn, Cache *cache, u64 last)
{
  isize error;
  if (!txn || !cache || !cache->entries || !cache->capacity || cache->capacity > CACHE_MAX ||
      !separate(txn, sizeof(*txn), cache, sizeof(*cache)) ||
      !separate(txn, sizeof(*txn), cache->entries, (u64) cache->capacity * sizeof(*cache->entries)))
    return -ERR_INVALID;
  error = journalinit(&txn->journal, cache->blocks, last);
  if (error)
    return error;
  txn->cache = cache;
  txn->snapshots = 0;
  txn->count = txn->metadata_count = txn->data_count = 0;
  txn->phase = TXN_IDLE;
  return 0;
}
static const Cachekey *
keyat(const Cachekey *metadata, u32 count, const Cachekey *data, u32 index)
{
  return index < count ? &metadata[index] : &data[index - count];
}
isize
txnprepare(Txn *txn, const Cachekey *metadata, u32 metadata_count, const Cachekey *data,
           u32 data_count, void *snapshots)
{
  const Cachekey *key;
  const u8       *view;
  Cacheblock     *entry;
  Journalblock   *block;
  u64             version, bytes;
  u32             i, j, count;
  isize           error, cleanup;
  if (!txn || !txn->cache)
    return -ERR_INVALID;
  if (txn->phase != TXN_IDLE || txn->journal.phase != JOURNAL_IDLE || txn->count)
    return -ERR_STATE;
  if (txn->journal.last == UINT64_MAX)
    return -ERR_OVERFLOW;
  if (!metadata_count || metadata_count > JOURNAL_META || data_count > JOURNAL_DATA ||
      data_count > CACHE_MAX - metadata_count)
    return -ERR_INVALID;
  count = metadata_count + data_count;
  bytes = (u64) count * CACHE_BYTES;
  if (count > txn->cache->capacity || !external(txn, snapshots, bytes) ||
      !external(txn, metadata, (u64) metadata_count * sizeof(*metadata)) ||
      !separate(metadata, (u64) metadata_count * sizeof(*metadata), snapshots, bytes) ||
      (data_count && (!external(txn, data, (u64) data_count * sizeof(*data)) ||
                      !separate(data, (u64) data_count * sizeof(*data), snapshots, bytes) ||
                      !separate(metadata, (u64) metadata_count * sizeof(*metadata), data,
                                (u64) data_count * sizeof(*data)))))
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    key = keyat(metadata, metadata_count, data, i);
    error = cacheview(txn->cache, key, &view, &version);
    if (error)
      return error;
    entry = &txn->cache->entries[key->index];
    if (!entry->dirty || entry->metadata != (u32) (i < metadata_count) ||
        entry->block < (i < metadata_count ? 65U : 99U))
      return -ERR_STATE;
    if (entry->io)
      return -ERR_BUSY;
    if (entry->serial == UINT64_MAX)
      return -ERR_OVERFLOW;
    for (j = 0; j < i; j++)
      if (key->index == keyat(metadata, metadata_count, data, j)->index)
        return -ERR_INVALID;
  }
  txn->snapshots = snapshots;
  txn->metadata_count = metadata_count;
  txn->data_count = data_count;
  txn->phase = TXN_PREPARED;
  for (i = 0; i < count; i++) {
    key = keyat(metadata, metadata_count, data, i);
    error =
        cachecapture(txn->cache, key, txn->snapshots + (u64) i * CACHE_BYTES, &txn->captures[i]);
    if (error) {
      cleanup = txnrelease(txn);
      return cleanup ? cleanup : error;
    }
    txn->count++;
    block = i < metadata_count ? &txn->metadata[i] : &txn->data[i - metadata_count];
    block->block = txn->cache->entries[key->index].block;
    block->bytes = txn->snapshots + (u64) i * CACHE_BYTES;
  }
  return 0;
}
isize
txnstart(Txn *txn, const void *header, const void *committed, const void *empty)
{
  Journalplan plan;
  isize       error;
  if (!txn || txn->phase != TXN_PREPARED || txn->count != txn->metadata_count + txn->data_count)
    return -ERR_STATE;
  if (!external(txn, header, JOURNAL_BYTES) || !external(txn, committed, JOURNAL_BYTES) ||
      !external(txn, empty, JOURNAL_BYTES))
    return -ERR_INVALID;
  plan.header = header;
  plan.committed = committed;
  plan.empty = empty;
  plan.metadata = txn->metadata;
  plan.data = txn->data;
  plan.metadata_count = txn->metadata_count;
  plan.data_count = txn->data_count;
  error = journalbegin(&txn->journal, &plan);
  if (!error)
    txn->phase = TXN_RUNNING;
  return error;
}
isize
txnnext(Txn *txn, Journalio *out)
{
  if (!txn || txn->phase != TXN_RUNNING)
    return -ERR_STATE;
  if (!external(txn, out, sizeof(*out)))
    return -ERR_INVALID;
  return journalnext(&txn->journal, out);
}
isize
txncomplete(Txn *txn, const Journalio *ticket, u32 success)
{
  if (!txn || txn->phase != TXN_RUNNING)
    return -ERR_STATE;
  if (!external(txn, ticket, sizeof(*ticket)))
    return -ERR_INVALID;
  return journalcomplete(&txn->journal, ticket, success);
}
isize
txnstop(Txn *txn)
{
  if (!txn || txn->phase != TXN_RUNNING)
    return -ERR_STATE;
  return journalstop(&txn->journal);
}
isize
txnrelease(Txn *txn)
{
  u32   success = 0, phase;
  isize error;
  if (!txn || (txn->phase != TXN_PREPARED && txn->phase != TXN_RUNNING))
    return -ERR_STATE;
  phase = txn->phase;
  if (phase == TXN_RUNNING) {
    if (txn->journal.active)
      return -ERR_WOULD_BLOCK;
    if (txn->journal.phase != JOURNAL_DONE && txn->journal.phase != JOURNAL_STOPPED)
      return -ERR_STATE;
    success = txn->journal.phase == JOURNAL_DONE;
  }
  while (txn->count) {
    error = cachecapturefinish(txn->cache, &txn->captures[txn->count - 1], success);
    if (error)
      return error;
    txn->count--;
  }
  if (phase == TXN_RUNNING) {
    error = journalrelease(&txn->journal);
    if (error)
      return error;
  }
  txn->snapshots = 0;
  txn->metadata_count = txn->data_count = 0;
  txn->phase = phase == TXN_PREPARED || success ? TXN_IDLE : TXN_STOPPED;
  return 0;
}
