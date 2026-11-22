#include "cache.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static int
external(Cache *cache, const void *out, u64 bytes)
{
  return cache && cache->entries && cache->capacity && cache->capacity <= CACHE_MAX &&
         cache->blocks && cache->cursor < cache->capacity &&
         separate(cache, sizeof(*cache), out, bytes) &&
         separate(cache->entries, (u64) cache->capacity * sizeof(*cache->entries), out, bytes);
}
static void
keyclear(Cachekey *key)
{
  key->generation = key->index = key->reserved = 0;
}
static void
bindingclear(Cacheblock *entry)
{
  entry->block = entry->version = entry->authorized = entry->serial = entry->io_version = 0;
  entry->references = entry->valid = entry->dirty = entry->metadata = entry->io = entry->bound = 0;
}
static int
valid(const Cacheblock *entry)
{
  if (!entry->bound)
    return !entry->block && !entry->version && !entry->authorized && !entry->serial &&
           !entry->io_version && !entry->references && !entry->valid && !entry->dirty &&
           !entry->metadata && !entry->io;
  return entry->bound == 1 && entry->generation && entry->valid <= 1 && entry->dirty <= 1 &&
         entry->metadata <= 1 && entry->io <= CACHE_CAPTURE &&
         entry->authorized <= entry->version &&
         (entry->valid ? entry->version != 0
                       : !entry->version && !entry->dirty && !entry->authorized) &&
         (entry->io ? entry->serial != 0 : entry->io_version == 0) &&
         (entry->io != CACHE_READ || !entry->valid) &&
         ((entry->io != CACHE_WRITE && entry->io != CACHE_CAPTURE) ||
          (entry->valid && entry->io_version && entry->io_version <= entry->version));
}
static isize
find(Cache *cache, const Cachekey *key, Cacheblock **out, u32 pinned)
{
  Cacheblock *entry;
  if (!external(cache, key, sizeof(*key)) || key->reserved || key->index >= cache->capacity)
    return -ERR_INVALID;
  entry = &cache->entries[key->index];
  if (!valid(entry) || (entry->bound && entry->block >= cache->blocks))
    return -ERR_STATE;
  if (!key->generation || !entry->bound || entry->generation != key->generation)
    return -ERR_STALE;
  if (pinned && !entry->references)
    return -ERR_STATE;
  *out = entry;
  return 0;
}
isize
cacheinit(Cache *cache, Cacheblock *storage, u32 count, u64 blocks)
{
  u32 i;
  if (!cache || !storage || !count || count > CACHE_MAX || !blocks ||
      !separate(cache, sizeof(*cache), storage, (u64) count * sizeof(*storage)))
    return -ERR_INVALID;
  cache->entries = storage;
  cache->capacity = count;
  cache->blocks = blocks;
  cache->cursor = 0;
  for (i = 0; i < count; i++) {
    storage[i].generation = 0;
    bindingclear(&storage[i]);
  }
  return 0;
}
isize
cacheget(Cache *cache, u64 block, u32 metadata, Cachekey *out)
{
  Cacheblock *entry;
  u32         i, index = CACHE_MAX;
  if (!external(cache, out, sizeof(*out)) || out->generation || out->index || out->reserved ||
      block >= cache->blocks || metadata > CACHE_META)
    return -ERR_INVALID;
  for (i = 0; i < cache->capacity; i++) {
    entry = &cache->entries[i];
    if (!valid(entry) || (entry->bound && entry->block >= cache->blocks))
      return -ERR_STATE;
    if (entry->bound && entry->block == block) {
      if (entry->metadata != metadata)
        return -ERR_STATE;
      if (entry->references == UINT32_MAX)
        return -ERR_OVERFLOW;
      entry->references++;
      out->generation = entry->generation;
      out->index = i;
      out->reserved = 0;
      return 0;
    }
  }
  for (i = 0; i < cache->capacity; i++) {
    u32 at = (cache->cursor + i) % cache->capacity;
    entry = &cache->entries[at];
    if (!entry->references && !entry->io && !entry->dirty && entry->generation < UINT64_MAX) {
      index = at;
      break;
    }
  }
  if (index == CACHE_MAX)
    return -ERR_CAPACITY;
  entry = &cache->entries[index];
  bindingclear(entry);
  entry->generation++;
  entry->block = block;
  entry->metadata = metadata;
  entry->bound = entry->references = 1;
  cache->cursor = (index + 1) % cache->capacity;
  out->generation = entry->generation;
  out->index = index;
  out->reserved = 0;
  return 0;
}
isize
cacheput(Cache *cache, Cachekey *owned)
{
  Cacheblock *entry;
  isize       error = find(cache, owned, &entry, 1);
  if (error)
    return error;
  entry->references--;
  keyclear(owned);
  return 0;
}
isize
cacheinvalidate(Cache *cache, Cachekey *owned)
{
  Cacheblock *entry;
  isize       error = find(cache, owned, &entry, 1);
  if (error)
    return error;
  if (entry->references != 1 || entry->io || entry->dirty)
    return -ERR_BUSY;
  bindingclear(entry);
  keyclear(owned);
  return 0;
}
isize
cacheview(Cache *cache, const Cachekey *key, const u8 **out, u64 *version)
{
  Cacheblock *entry;
  isize       error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (!external(cache, out, sizeof(*out)) || !external(cache, version, sizeof(*version)) ||
      !separate(key, sizeof(*key), out, sizeof(*out)) ||
      !separate(key, sizeof(*key), version, sizeof(*version)) ||
      !separate(out, sizeof(*out), version, sizeof(*version)))
    return -ERR_INVALID;
  if (!entry->valid)
    return -ERR_WOULD_BLOCK;
  *out = entry->data;
  *version = entry->version;
  return 0;
}
isize
cachespanbegin(Cache *cache, u64 first, u64 next, u32 offset, u32 bytes, Cachespan *span)
{
  isize error = 0, cleanup;
  if (offset >= CACHE_BYTES || !bytes || bytes > CACHE_BYTES ||
      !external(cache, span, sizeof(*span)))
    return -ERR_INVALID;
  if (span->cache || span->keys[0].generation || span->keys[1].generation)
    return -ERR_BUSY;
  if ((bytes <= CACHE_BYTES - offset && next) || first >= cache->blocks || next >= cache->blocks)
    return -ERR_INVALID;
  span->cache = cache;
  span->offset = offset;
  span->bytes = bytes;
  if (first)
    error = cacheget(cache, first, CACHE_DATA, &span->keys[0]);
  if (!error && next)
    error = cacheget(cache, next, CACHE_DATA, &span->keys[1]);
  if (error) {
    cleanup = cachespanend(span);
    if (cleanup)
      return cleanup;
  }
  return error;
}

isize
cachespancopy(const Cachespan *span, void *out)
{
  const u8 *data[2] = {NULL, NULL};
  u8       *destination = out;
  u64       version;
  u32       length, i, j;
  isize     error;
  if (!span || !span->bytes || span->bytes > CACHE_BYTES || span->offset >= CACHE_BYTES ||
      !external(span->cache, out, span->bytes) || !separate(span, sizeof(*span), out, span->bytes))
    return -ERR_INVALID;
  length = MIN(span->bytes, CACHE_BYTES - span->offset);
  for (i = 0; i < 2; i++)
    if (span->keys[i].generation) {
      error = cacheview(span->cache, &span->keys[i], &data[i], &version);
      if (error)
        return error;
    }
  for (i = 0; i < span->bytes; i++) {
    j = i < length ? 0 : 1;
    destination[i] = data[j] ? data[j][j ? i - length : span->offset + i] : 0;
  }
  return 0;
}

isize
cachespanend(Cachespan *span)
{
  u32   i;
  isize error;
  if (!span || !span->cache)
    return -ERR_INVALID;
  for (i = 0; i < 2; i++)
    if (span->keys[i].generation) {
      error = cacheput(span->cache, &span->keys[i]);
      if (error)
        return error;
    }
  span->cache = NULL;
  span->offset = span->bytes = 0;
  return 0;
}

isize
cachecopydata(Cache *cache, u64 first, u64 next, u32 offset, void *out, u32 bytes)
{
  Cachespan span = {0};
  isize     error, cleanup;
  if (!external(cache, out, bytes))
    return -ERR_INVALID;
  error = cachespanbegin(cache, first, next, offset, bytes, &span);
  if (error)
    return error;
  error = cachespancopy(&span, out);
  cleanup = cachespanend(&span);
  if (!error)
    error = cleanup;
  return error;
}

isize
cacheedit(Cache *cache, const Cachekey *key, u32 offset, const void *data, u32 bytes)
{
  Cacheblock        *entry;
  const volatile u8 *source = data;
  u32                i;
  isize              error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (offset > CACHE_BYTES || bytes > CACHE_BYTES - offset ||
      (bytes && (!external(cache, data, bytes) || !separate(key, sizeof(*key), data, bytes))))
    return -ERR_INVALID;
  if (!entry->valid)
    return -ERR_WOULD_BLOCK;
  if (!bytes)
    return 0;
  if (entry->version == UINT64_MAX)
    return -ERR_OVERFLOW;
  for (i = 0; i < bytes; i++)
    entry->data[offset + i] = source[i];
  entry->version++;
  entry->dirty = 1;
  return 0;
}
static isize
begin(Cache *cache, const Cachekey *key, Cacheio *out, u32 kind, Cacheblock **entry)
{
  isize error = find(cache, key, entry, 1);
  if (error)
    return error;
  if (!external(cache, out, sizeof(*out)) || !separate(key, sizeof(*key), out, sizeof(*out)))
    return -ERR_INVALID;
  if ((*entry)->io)
    return -ERR_BUSY;
  if ((*entry)->serial == UINT64_MAX)
    return -ERR_OVERFLOW;
  out->key.generation = key->generation;
  out->key.index = key->index;
  out->key.reserved = 0;
  out->serial = ++(*entry)->serial;
  out->version = (*entry)->version;
  out->kind = kind;
  out->reserved = 0;
  (*entry)->io = kind;
  (*entry)->io_version = out->version;
  return 0;
}
isize
cachereadbegin(Cache *cache, const Cachekey *key, Cacheio *out, u8 **destination)
{
  Cacheblock *entry;
  isize       error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (!external(cache, destination, sizeof(*destination)) ||
      !separate(key, sizeof(*key), destination, sizeof(*destination)) ||
      !separate(out, sizeof(*out), destination, sizeof(*destination)))
    return -ERR_INVALID;
  if (entry->valid)
    return -ERR_STATE;
  error = begin(cache, key, out, CACHE_READ, &entry);
  if (!error)
    *destination = entry->data;
  return error;
}
isize
cachefetch(Cache *cache, u64 block, u32 metadata, Cacheio *out, u8 **destination)
{
  Cachekey  key = {0};
  const u8 *data;
  u64       version;
  isize     error, cleanup;
  if (!external(cache, out, sizeof(*out)) || !external(cache, destination, sizeof(*destination)) ||
      !separate(out, sizeof(*out), destination, sizeof(*destination)))
    return -ERR_INVALID;
  error = cacheget(cache, block, metadata, &key);
  if (error)
    return error;
  error = cacheview(cache, &key, &data, &version);
  if (error == -ERR_WOULD_BLOCK) {
    error = cachereadbegin(cache, &key, out, destination);
    error = !error ? 1 : error == -ERR_BUSY ? -ERR_WOULD_BLOCK : error;
  }
  cleanup = cacheput(cache, &key);
  return cleanup ? cleanup : error;
}

isize
cachefillstart(Cachefill *fill, Cache *cache, u64 block, u32 metadata, Cacheio *out,
               u8 **destination)
{
  isize error;
  u32   i;
  if (!external(cache, fill, sizeof(*fill)) || !separate(fill, sizeof(*fill), out, sizeof(*out)) ||
      !separate(fill, sizeof(*fill), destination, sizeof(*destination)))
    return -ERR_INVALID;
  if (fill->cache)
    return -ERR_BUSY;
  error = cachefetch(cache, block, metadata, out, destination);
  if (error == 1) {
    for (i = 0; i < sizeof(*out); i++)
      ((volatile u8 *) &fill->ticket)[i] = ((const u8 *) out)[i];
    fill->cache = cache;
  }
  return error;
}

static int
fillmatches(const Cachefill *fill, const Cacheio *ticket)
{
  const Cacheio *owned;
  if (!fill || !ticket || !fill->cache)
    return 0;
  owned = &fill->ticket;
  return ticket->key.index == owned->key.index && ticket->key.generation == owned->key.generation &&
         ticket->serial == owned->serial && ticket->version == owned->version &&
         ticket->kind == owned->kind;
}

isize
cachefillfinish(Cachefill *fill, const Cacheio *ticket, u32 success)
{
  isize error;
  if (!fillmatches(fill, ticket))
    return -ERR_STALE;
  error = cachecomplete(fill->cache, ticket, success);
  if (!error)
    fill->cache = NULL;
  return error;
}

isize
cachewritebegin(Cache *cache, const Cachekey *key, void *snapshot, Cacheio *out)
{
  Cacheblock  *entry;
  volatile u8 *to = snapshot;
  u32          i;
  isize        error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (!external(cache, snapshot, CACHE_BYTES) ||
      !separate(key, sizeof(*key), snapshot, CACHE_BYTES) ||
      !separate(out, sizeof(*out), snapshot, CACHE_BYTES))
    return -ERR_INVALID;
  if (!entry->valid || !entry->dirty || (entry->metadata && entry->authorized != entry->version))
    return -ERR_STATE;
  error = begin(cache, key, out, CACHE_WRITE, &entry);
  if (error)
    return error;
  for (i = 0; i < CACHE_BYTES; i++)
    to[i] = entry->data[i];
  return 0;
}
static isize
ticketentry(Cache *cache, const Cacheio *ticket, Cacheblock **entry, u32 success)
{
  isize error;
  if (!external(cache, ticket, sizeof(*ticket)) || ticket->reserved || success > 1)
    return -ERR_INVALID;
  /* find requires an external key; the ticket's embedded key is external too. */
  error = find(cache, &ticket->key, entry, 0);
  if (error)
    return error;
  if (!ticket->serial || ticket->serial != (*entry)->serial || ticket->kind != (*entry)->io ||
      ticket->version != (*entry)->io_version || !(*entry)->io)
    return -ERR_STALE;
  return 0;
}

isize
cachefillcopy(Cachefill *fill, const Cacheio *ticket, const void *block, u32 bytes)
{
  Cacheblock *entry;
  const u8   *source = block;
  u32         i;
  isize       error;
  if (!fillmatches(fill, ticket))
    return -ERR_STALE;
  if (bytes != CACHE_BYTES || !external(fill->cache, block, bytes))
    return -ERR_INVALID;
  error = ticketentry(fill->cache, ticket, &entry, 1);
  if (error)
    return error;
  if (entry->io != CACHE_READ)
    return -ERR_STATE;
  for (i = 0; i < bytes; i++)
    entry->data[i] = source[i];
  return cachefillfinish(fill, ticket, 1);
}

isize
cachecomplete(Cache *cache, const Cacheio *ticket, u32 success)
{
  Cacheblock *entry;
  isize       error = ticketentry(cache, ticket, &entry, success);
  if (error)
    return error;
  if (entry->io == CACHE_CAPTURE)
    return -ERR_STATE;
  if (entry->io == CACHE_READ) {
    if (success) {
      entry->valid = 1;
      entry->version = 1;
    }
  } else if (success && entry->version == ticket->version)
    entry->dirty = 0;
  entry->io = 0;
  entry->io_version = 0;
  return 0;
}
isize
cachecapture(Cache *cache, const Cachekey *key, void *snapshot, Cacheio *out)
{
  Cacheblock  *entry;
  volatile u8 *to = snapshot;
  u32          i;
  isize        error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (!external(cache, snapshot, CACHE_BYTES) ||
      !separate(key, sizeof(*key), snapshot, CACHE_BYTES) ||
      !separate(out, sizeof(*out), snapshot, CACHE_BYTES))
    return -ERR_INVALID;
  if (!entry->valid || !entry->dirty)
    return -ERR_STATE;
  error = begin(cache, key, out, CACHE_CAPTURE, &entry);
  if (error)
    return error;
  for (i = 0; i < CACHE_BYTES; i++)
    to[i] = entry->data[i];
  return 0;
}
isize
cachecapturefinish(Cache *cache, Cacheio *owned, u32 success)
{
  Cacheblock *entry;
  isize       error = ticketentry(cache, owned, &entry, success);
  if (error)
    return error;
  if (entry->io != CACHE_CAPTURE)
    return -ERR_STATE;
  if (success && entry->version == owned->version)
    entry->dirty = 0;
  entry->io = 0;
  entry->io_version = 0;
  keyclear(&owned->key);
  owned->serial = owned->version = 0;
  owned->kind = owned->reserved = 0;
  return 0;
}
isize
cacheauthorize(Cache *cache, const Cachekey *key, u64 version)
{
  Cacheblock *entry;
  isize       error = find(cache, key, &entry, 1);
  if (error)
    return error;
  if (!entry->metadata || !entry->valid || !entry->dirty || !version || version != entry->version)
    return -ERR_STATE;
  entry->authorized = version;
  return 0;
}
