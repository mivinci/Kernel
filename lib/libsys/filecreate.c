#include "private/filecreate.h"
#include "private/syscall.h"

static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static void
clear(void *out, u64 bytes)
{
  u64 i;
  for (i = 0; i < bytes; i++)
    ((u8 *) out)[i] = 0;
}
static void
copy(void *out, const void *in, u64 bytes)
{
  volatile u8       *to = out;
  const volatile u8 *from = in;
  u64                i;
  for (i = 0; i < bytes; i++)
    to[i] = from[i];
}
static int
external(Filepool *pool, Connwork *work, Conncache *cache, const void *out, u64 bytes)
{
  return separate(pool, sizeof(*pool), out, bytes) && separate(work, sizeof(*work), out, bytes) &&
         separate(cache, sizeof(*cache), out, bytes);
}
static isize
target(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key, Createop **out)
{
  Createop *create;
  Slotop   *op;
  isize     error;
  if (!pool || !work || !cache || !external(pool, work, cache, key, sizeof(*key)) ||
      !separate(pool, sizeof(*pool), work, sizeof(*work)) ||
      !separate(pool, sizeof(*pool), cache, sizeof(*cache)))
    return -ERR_INVALID;
  error = sysopcheck(work, cache, key);
  if (!error)
    error = sysfilerefcheck(pool, work->state.self);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  create = &work->state.creators[key->lease.slot];
  if (!(op->flags & SYSOP_CREATOR) || key->lease.span != 1 ||
      (op->opcode != FS_OPEN && op->opcode != FS_DUPREF && op->opcode != FS_PIPE) ||
      create->phase < SYSFILE_CREATE_PREPARED || create->phase > SYSFILE_CREATE_RESOLVED ||
      create->count != (op->opcode == FS_PIPE ? 2U : 1U) ||
      create->flags & ~SYSFILE_CREATE_PEER_ENDED || create->disposition > SYSFILE_RESULT_ENDED ||
      (create->phase != SYSFILE_CREATE_RESOLVED &&
       op->file != (create->source ? create->source : create->files[0])))
    return -ERR_STATE;
  if (create->phase != SYSFILE_CREATE_RESOLVED) {
    if (create->stage.count != create->count || create->stage.files[0] != create->files[0] ||
        create->stage.files[1] != create->files[1])
      return -ERR_STATE;
    error = sysfilerefstaged(pool, work->state.self, &create->stage);
    if (error)
      return error;
  }
  *out = create;
  return 0;
}
isize
sysfilecreatecheck(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Createop *create;
  return target(pool, work, cache, key, &create);
}
isize
sysfilecreateabort(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key, u64 *source)
{
  Createop *create;
  Slotop   *op;
  u64       carrier, original;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, source, sizeof(*source)) || *source ||
      !separate(key, sizeof(*key), source, sizeof(*source)))
    return -ERR_INVALID;
  op = &work->state.operations[key->lease.slot];
  if (op->phase != SYSOP_PREPARED || create->phase != SYSFILE_CREATE_PREPARED)
    return -ERR_STATE;
  original = create->source;
  op->flags &= ~SYSOP_CREATOR;
  error = sysopabort(work, cache, key, &carrier);
  if (error) {
    op->flags |= SYSOP_CREATOR;
    return error;
  }
  error = sysfilerefabort(pool, work->state.self, &create->stage);
  if (error)
    return error;
  clear(create, sizeof(*create));
  *source = original;
  return 0;
}
isize
sysfilecreateprepare(Filepool *pool, Connwork *work, Conncache *cache, u32 context, u64 owner,
                     u64 opcode, u64 object, u64 *owned_source, const u64 args[8],
                     const void *payload, u64 bytes, Opkey *key, Msg *message)
{
  Filestage stage;
  Fileref   source;
  Fileslot  descriptor;
  Opkey     prepared;
  Createop *create;
  u64       carrier, restored = 0;
  u32       i;
  isize     error;
  if (!pool || !work || !cache || context > SYSFILE_SIGNAL2 || !owner ||
      (opcode != FS_OPEN && opcode != FS_DUPREF && opcode != FS_PIPE) || bytes > FS_SLOT_BYTES ||
      !separate(pool, sizeof(*pool), work, sizeof(*work)) ||
      !separate(pool, sizeof(*pool), cache, sizeof(*cache)) ||
      !external(pool, work, cache, owned_source, sizeof(*owned_source)) ||
      !external(pool, work, cache, args, 8 * sizeof(u64)) ||
      !external(pool, work, cache, key, sizeof(*key)) ||
      !external(pool, work, cache, message, sizeof(*message)) ||
      !separate(owned_source, sizeof(*owned_source), key, sizeof(*key)) ||
      !separate(owned_source, sizeof(*owned_source), message, sizeof(*message)) ||
      !separate(args, 8 * sizeof(u64), key, sizeof(*key)) ||
      !separate(args, 8 * sizeof(u64), message, sizeof(*message)) ||
      !separate(args, 8 * sizeof(u64), owned_source, sizeof(*owned_source)) ||
      !separate(key, sizeof(*key), message, sizeof(*message)))
    return -ERR_INVALID;
  if (bytes && (!external(pool, work, cache, payload, bytes) ||
                !separate(payload, bytes, key, sizeof(*key)) ||
                !separate(payload, bytes, message, sizeof(*message)) ||
                !separate(payload, bytes, owned_source, sizeof(*owned_source))))
    return -ERR_INVALID;
  if (opcode == FS_OPEN) {
    if (!bytes || args[2] || args[3] != bytes || ((const u8 *) payload)[bytes - 1])
      return -ERR_INVALID;
    for (i = 0; i + 1 < bytes; i++)
      if (!((const u8 *) payload)[i])
        return -ERR_INVALID;
    for (i = 4; i < 8; i++)
      if (args[i])
        return -ERR_INVALID;
  } else {
    if (bytes || (opcode == FS_PIPE && (object || *owned_source || args[1] != 8192)))
      return -ERR_INVALID;
    for (i = opcode == FS_PIPE ? 2U : 1U; i < 8; i++)
      if (args[i])
        return -ERR_INVALID;
  }
  if (*owned_source) {
    error = sysfilerefview(pool, work->state.self, *owned_source, &source);
    if (error)
      return error;
    if (!source.pins || source.connection.generation != work->state.tag.generation ||
        source.connection.index != work->state.tag.index || source.session != object ||
        (opcode == FS_DUPREF && source.reference != args[0]))
      return -ERR_STATE;
  } else if (object || opcode == FS_DUPREF)
    return -ERR_INVALID;
  clear(&stage, sizeof(stage));
  error = sysfilerefreserve(pool, work->state.self, opcode == FS_PIPE ? 2 : 1, &stage);
  if (error)
    return error;
  carrier = *owned_source ? *owned_source : stage.files[0];
  error = sysopprepare(work, cache, context, 1, owner, &carrier, object, opcode, 0, &prepared);
  if (error) {
    if (sysfilerefabort(pool, work->state.self, &stage))
      return -ERR_STATE;
    return error;
  }
  create = &work->state.creators[prepared.lease.slot];
  clear(create, sizeof(*create));
  copy(&create->stage, &stage, sizeof(stage));
  create->files[0] = stage.files[0];
  create->files[1] = stage.files[1];
  create->source = *owned_source;
  create->count = stage.count;
  create->phase = SYSFILE_CREATE_PREPARED;
  work->state.operations[prepared.lease.slot].flags |= SYSOP_CREATOR;
  *owned_source = 0;
  clear(&descriptor, sizeof(descriptor));
  descriptor.seq = prepared.lease.seq;
  descriptor.state = FS_SLOT_SUBMITTED;
  descriptor.payload_bytes = bytes;
  descriptor.object = object;
  descriptor.span = 1;
  for (i = 0; i < 8; i++)
    descriptor.args[i] = args[i];
  error = __sys_copy((void *) (uintptr_t) (work->state.map.address + FS_DESC_OFFSET +
                                           prepared.lease.slot * FS_DESC_BYTES),
                     &descriptor, sizeof(descriptor));
  if (!error && bytes)
    error = __sys_copy((void *) (uintptr_t) (work->state.map.address + FS_DATA_OFFSET +
                                             prepared.lease.slot * FS_SLOT_BYTES),
                       payload, bytes);
  if (error) {
    if (sysfilecreateabort(pool, work, cache, &prepared, &restored))
      return -ERR_STATE;
    *owned_source = restored;
    return error;
  }
  copy(key, &prepared, sizeof(prepared));
  message->words[0] = opcode;
  message->words[1] = work->state.map.connid;
  message->words[2] = prepared.lease.seq;
  message->words[3] = prepared.lease.slot;
  return 0;
}
isize
sysfilecreateresult(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Createop *create;
  Slotop   *op;
  Filevalue values[2];
  Fileref   source;
  Connpeer  peer;
  Handle    call = 0;
  u64       pairs[4], reference = 0;
  isize     error = target(pool, work, cache, key, &create);
  u32       i, j, k;
  if (error || create->phase >= SYSFILE_CREATE_CAPTURED)
    return error;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (op->status == -ERR_CANCELLED) {
    if (op->phase != SYSOP_RESULT || op->flags & SYSOP_BAD_RESULT || op->kind != IPC_NONE ||
        op->resource)
      return -ERR_INVALID;
    for (i = 0; i < ARRAY_SIZE(op->words); i++)
      if (op->words[i])
        return -ERR_INVALID;
    error = sysopfinished(work, cache, key);
    if (error)
      return error;
    create->error = -ERR_CANCELLED;
    create->phase = SYSFILE_CREATE_CAPTURED;
    return 0;
  }
  if (op->status)
    return op->status;
  if (op->phase != SYSOP_RESULT || op->flags & SYSOP_BAD_RESULT || op->words[1] != key->lease.seq)
    return -ERR_INVALID;
  clear(values, sizeof(values));
  clear(&peer, sizeof(peer));
  if (op->kind == IPC_NONE) {
    if (op->resource || (isize) op->words[0] > 0)
      return -ERR_INVALID;
    if ((isize) op->words[0] < 0) {
      if (op->words[2] || op->words[3])
        return -ERR_INVALID;
      error = sysopfinished(work, cache, key);
      if (error)
        return error;
      create->error = (isize) op->words[0];
      create->phase = SYSFILE_CREATE_CAPTURED;
      return 0;
    }
    if (op->opcode == FS_DUPREF) {
      if (!op->words[2] || op->words[3])
        return -ERR_INVALID;
      error = sysfilerefview(pool, work->state.self, create->source, &source);
      if (error)
        return error;
      if (op->words[2] == source.reference)
        return -ERR_INVALID;
      values[0].session = op->object;
      values[0].reference = op->words[2];
      error = sysopfinished(work, cache, key);
    } else if (op->opcode == FS_OPEN) {
      if (!op->words[2] || op->words[3] != 8)
        return -ERR_INVALID;
      error = sysopcopy(work, cache, key, 0, 8, &reference);
      values[0].session = op->words[2];
      values[0].reference = reference;
    } else {
      if (op->words[2] != 2 || op->words[3] != 32)
        return -ERR_INVALID;
      error = sysopcopy(work, cache, key, 0, 32, pairs);
      if (!error)
        for (i = 0; i < 2; i++) {
          values[i].session = pairs[i * 2];
          values[i].reference = pairs[i * 2 + 1];
        }
    }
    if (error)
      return error;
    for (i = 0; i < create->count; i++) {
      copy(&values[i].connection, &work->state.tag, sizeof(work->state.tag));
      if (!values[i].session || !values[i].reference)
        return -ERR_INVALID;
    }
    if (create->count == 2 && values[0].session == values[1].session &&
        values[0].reference == values[1].reference)
      return -ERR_INVALID;
    for (i = 0; i < create->count; i++) {
      for (j = 0; j < SYSFILE_REFS; j++) {
        const Fileref *held = &pool->entries[j];
        if ((held->state == SYSFILE_REF_LIVE || held->state == SYSFILE_REF_CLOSING) &&
            held->connection.generation == work->state.tag.generation &&
            held->connection.index == work->state.tag.index && held->session == values[i].session &&
            held->reference == values[i].reference)
          return -ERR_INVALID;
      }
      for (j = 0; j < FS_CONN_SLOTS; j++) {
        const Createop *held = &work->state.creators[j];
        if (j == key->lease.slot || held->phase != SYSFILE_CREATE_CAPTURED || held->error ||
            held->token)
          continue;
        if (!held->count || held->count > 2)
          return -ERR_STATE;
        for (k = 0; k < held->count; k++)
          if (held->values[k].session == values[i].session &&
              held->values[k].reference == values[i].reference)
            return -ERR_INVALID;
      }
    }
  } else if (op->kind == IPC_RESOURCE_REPLY && op->opcode == FS_OPEN && op->resource &&
             op->words[2] && op->words[3]) {
    error = sysconnpeer(op->resource, &peer);
    if (!error)
      error = sysopcopy(work, cache, key, 0, 8, &reference);
    if (error)
      return error;
    if (!reference)
      return -ERR_INVALID;
    error = sysopresource(work, cache, key, &call);
    if (error)
      return error;
    values[0].session = op->words[2];
    values[0].reference = reference;
    values[0].token = op->words[3];
    create->token = op->words[3];
  } else
    return -ERR_INVALID;
  for (i = 0; i < create->count; i++)
    copy(&create->values[i], &values[i], sizeof(values[i]));
  copy(&create->peer, &peer, sizeof(peer));
  create->call = call;
  create->phase = SYSFILE_CREATE_CAPTURED;
  return 0;
}
isize
sysfilecreatefailed(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Createop *create;
  Slotop   *op;
  Connentry entry;
  u32       i;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (work->state.ended != 1)
    return -ERR_STATE;
  error = sysconnview(cache, work->state.tag, &entry);
  if (error)
    return error;
  if (entry.state != SYSCONN_DRAINING || entry.peer.pid != work->state.peer.pid ||
      entry.peer.image != work->state.peer.image)
    return -ERR_STALE;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if ((op->phase != SYSOP_RESULT && op->phase != SYSOP_ACKED) || op->status != -ERR_PEER ||
      op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT || create->call ||
      create->token)
    return -ERR_STATE;
  for (i = 0; i < 4; i++)
    if (op->words[i])
      return -ERR_STATE;
  if (create->phase >= SYSFILE_CREATE_CAPTURED)
    return create->error == -ERR_PEER ? 0 : -ERR_STATE;
  if (op->phase != SYSOP_RESULT)
    return -ERR_STATE;
  error = sysopfinished(work, cache, key);
  if (error)
    return error;
  create->error = -ERR_PEER;
  create->phase = SYSFILE_CREATE_CAPTURED;
  return 0;
}
isize
sysfilecreateack(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  isize error = sysfilecreateresult(pool, work, cache, key);
  if (error || work->state.operations[key->lease.slot].phase == SYSOP_ACKED)
    return error;
  error = sysopsaved(work, cache, key);
  return error ? error : sysopack(work, cache, key);
}
isize
sysfilecreatesettle(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key, u32 closing)
{
  Createop *create;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (closing > 1)
    return -ERR_INVALID;
  if (create->phase == SYSFILE_CREATE_RESOLVED)
    return create->disposition == closing ? 0 : -ERR_STATE;
  if (create->phase != SYSFILE_CREATE_CAPTURED)
    return -ERR_STATE;
  if (create->call || create->token)
    return -ERR_NOTSUP;
  if (!closing && !create->error && work->state.operations[key->lease.slot].flags & SYSOP_APP_ENDED)
    return -ERR_STATE;
  if (create->error)
    error = sysfilerefabort(pool, work->state.self, &create->stage);
  else
    error = sysfilerefcommit(pool, work->state.self, &create->stage, create->values, closing);
  if (error)
    return error;
  /* The pool now owns compensation; those IDs are never returned for fd
     publication. A successful live result remains owned here until finish. */
  if (closing || create->error)
    create->files[0] = create->files[1] = 0;
  create->disposition = closing;
  create->phase = SYSFILE_CREATE_RESOLVED;
  return 0;
}
static int
samepeer(Connpeer a, Connpeer b)
{
  return a.pid == b.pid && a.image == b.image;
}
static int
sameclaim(Connclaim a, Connclaim b)
{
  return a.tag.generation == b.tag.generation && a.tag.index == b.tag.index && !a.tag.reserved &&
         !b.tag.reserved && a.seq == b.seq && a.owner == b.owner;
}
static isize
resource(Createop *create, Slotop *op)
{
  if (create->flags & SYSFILE_CREATE_PEER_ENDED)
    return -ERR_PEER;
  if (op->phase != SYSOP_ACKED || op->opcode != FS_OPEN || op->kind != IPC_RESOURCE_REPLY ||
      create->phase != SYSFILE_CREATE_CAPTURED || create->count != 1 || create->error ||
      !create->token || !create->peer.pid || !create->peer.image)
    return -ERR_STATE;
  return 0;
}
static isize
backendentry(Filepool *pool, Connwork *work, Conncache *cache, Createop *create, Connwork *backend,
             Connentry *entry)
{
  isize error;
  if (!backend || (backend != work && !separate(work, sizeof(*work), backend, sizeof(*backend))) ||
      !separate(pool, sizeof(*pool), backend, sizeof(*backend)) ||
      !separate(backend, sizeof(backend->state), cache, sizeof(*cache)))
    return -ERR_INVALID;
  if (!samepeer(backend->state.self, work->state.self) ||
      !samepeer(backend->state.peer, create->peer))
    return -ERR_STALE;
  error = sysconnview(cache, backend->state.tag, entry);
  if (error)
    return error;
  if (!samepeer(entry->peer, create->peer))
    return -ERR_STALE;
  return 0;
}
isize
sysfilecreatebackend(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                     Conntag *out)
{
  Createop *create;
  Connentry entry;
  Conntag   tag;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, out, sizeof(*out)) ||
      !separate(key, sizeof(*key), out, sizeof(*out)))
    return -ERR_INVALID;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (!error)
    error = sysconnbind(cache, create->peer, &tag);
  if (!error)
    error = sysconnview(cache, tag, &entry);
  if (error)
    return error;
  if (entry.owner && entry.owner != create->files[0])
    return -ERR_BUSY;
  copy(out, &tag, sizeof(tag));
  return 0;
}
static isize
backendview(Filepool *pool, Connwork *work, Conncache *cache, Createop *create, Connwork *backend,
            Connentry *entry)
{
  isize error = backendentry(pool, work, cache, create, backend, entry);
  return error ? error : entry->state == SYSCONN_DRAINING ? -ERR_PEER : 0;
}
static int
bootmatches(Bootstrap *boot, Connclaim claim, Createop *create, u32 opcode)
{
  return !sysbootcheck(boot) && boot->state == SYSBOOT_ACKED && boot->opcode == opcode &&
         boot->token == create->token && sameclaim(boot->claim, claim) &&
         claim.owner == create->files[0];
}
isize
sysfilecreateattachprepare(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                           Connwork *backend, u64 deadline, Connclaim *claim, Handle *call)
{
  Createop *create;
  Connentry entry;
  Connclaim prepared;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, claim, sizeof(*claim)) ||
      !external(pool, work, cache, call, sizeof(*call)) ||
      !separate(claim, sizeof(*claim), call, sizeof(*call)) ||
      !separate(key, sizeof(*key), claim, sizeof(*claim)) ||
      !separate(key, sizeof(*key), call, sizeof(*call)) || !backend ||
      !separate(backend, sizeof(*backend), key, sizeof(*key)) ||
      !separate(backend, sizeof(*backend), claim, sizeof(*claim)) ||
      !separate(backend, sizeof(*backend), call, sizeof(*call)))
    return -ERR_INVALID;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (!error)
    error = backendview(pool, work, cache, create, backend, &entry);
  if (error)
    return error;
  if (!create->call)
    return -ERR_STATE;
  if (sysbootcheck(&backend->state.bootstrap))
    return -ERR_STATE;
  if (backend->state.bootstrap.state != SYSBOOT_FREE)
    return -ERR_BUSY;
  error = entry.state == SYSCONN_READY
              ? sysconnreusebegin(cache, backend->state.tag, create->files[0], &prepared)
              : sysconnbegin(cache, backend->state.tag, create->files[0], &prepared);
  if (error)
    return error;
  error = sysbootprepare(&backend->state.bootstrap, prepared, FS_ATTACH, create->token, deadline);
  if (error) {
    if (sysconnrollback(cache, prepared))
      return -ERR_STATE;
    return error;
  }
  copy(claim, &prepared, sizeof(prepared));
  *call = create->call;
  return 0;
}
static isize
keepcall(Createop *create, Connwork *backend, Conncache *cache)
{
  Handle   owned, redundant = 0;
  Connpeer peer;
  isize    error;
  if (!create->call) {
    error = sysconnpeer(backend->state.call, &peer);
    return error ? error : samepeer(peer, create->peer) ? 0 : -ERR_DENIED;
  }
  owned = create->call;
  error = sysconnworktakecall(backend, cache, &owned, &redundant);
  if (error)
    return error;
  create->call = redundant;
  if (redundant) {
    error = __sys_free(redundant);
    if (error)
      return error;
    create->call = 0;
  }
  return 0;
}
isize
sysfilecreatekeepcall(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                      Connwork *backend, Connclaim claim)
{
  Createop *create;
  Connentry entry;
  Bootreply reply;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!backend || !separate(backend, sizeof(*backend), key, sizeof(*key)))
    return -ERR_INVALID;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (!error)
    error = backendview(pool, work, cache, create, backend, &entry);
  if (error)
    return error;
  if (!bootmatches(&backend->state.bootstrap, claim, create, FS_ATTACH) ||
      claim.tag.generation != backend->state.tag.generation ||
      claim.tag.index != backend->state.tag.index || entry.sequence != claim.seq ||
      entry.owner != claim.owner)
    return -ERR_STALE;
  error = sysbootreply(&backend->state.bootstrap, claim,
                       entry.state == SYSCONN_READY ? entry.connid : 0, &reply);
  return error ? error : keepcall(create, backend, cache);
}
isize
sysfilecreateattached(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                      Connwork *backend, Connclaim claim, Handle space, Bootmap *map, u32 closing)
{
  Createop  *create;
  Connentry  entry;
  Bootstrap *boot;
  Bootreply  reply;
  u32        i;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (closing > 1 || !external(pool, work, cache, map, sizeof(*map)) || !backend ||
      !separate(backend, sizeof(*backend), key, sizeof(*key)) ||
      !separate(backend, sizeof(*backend), map, sizeof(*map)) ||
      !separate(key, sizeof(*key), map, sizeof(*map)))
    return -ERR_INVALID;
  if (create->phase == SYSFILE_CREATE_RESOLVED)
    return create->peer.pid && create->disposition == closing ? 0 : -ERR_STATE;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (!error)
    error = backendview(pool, work, cache, create, backend, &entry);
  if (error)
    return error;
  boot = &backend->state.bootstrap;
  if (!bootmatches(boot, claim, create, FS_ATTACH) ||
      claim.tag.generation != backend->state.tag.generation ||
      claim.tag.index != backend->state.tag.index || entry.sequence != claim.seq ||
      (entry.owner && entry.owner != claim.owner))
    return -ERR_STALE;
  if (!closing && work->state.operations[key->lease.slot].flags & SYSOP_APP_ENDED)
    return -ERR_STATE;
  if (entry.owner) {
    error = sysbootreply(boot, claim, entry.state == SYSCONN_READY ? entry.connid : 0, &reply);
    if (error)
      return error;
    if (entry.state == SYSCONN_READY && (map->address || map->bytes || map->connid ||
                                         map->serverimage || map->mapped || map->verified))
      return -ERR_INVALID;
    if (entry.state == SYSCONN_CONNECTING &&
        (map->mapped != 1 || map->verified != 1 || !map->address || (map->address & PAGE_MASK) ||
         map->bytes != reply.bytes || map->connid != reply.connid ||
         map->serverimage != create->peer.image))
      return -ERR_INVALID;
  }
  for (i = 0; i < SYSFILE_REFS; i++) {
    const Fileref *held = &pool->entries[i];
    if ((held->state == SYSFILE_REF_LIVE || held->state == SYSFILE_REF_CLOSING) &&
        held->connection.generation == claim.tag.generation &&
        held->connection.index == claim.tag.index && held->session == create->values[0].session &&
        held->reference == create->values[0].reference)
      return -ERR_INVALID;
  }
  error = keepcall(create, backend, cache);
  if (error)
    return error;
  if (entry.owner) {
    error = entry.state == SYSCONN_READY ? sysconnworkreuse(backend, cache, claim)
                                         : sysconnworkadopt(backend, cache, claim, space, map);
    if (error)
      return error;
  } else if (entry.state != SYSCONN_READY || entry.sequence != claim.seq ||
             !backend->state.memory || !backend->state.map.verified)
    return -ERR_STALE;
  copy(&create->values[0].connection, &backend->state.tag, sizeof(backend->state.tag));
  error = sysfilerefcommit(pool, work->state.self, &create->stage, create->values, closing);
  if (error)
    return error;
  error = sysbootfinish(boot, claim);
  if (error)
    return error;
  if (closing)
    create->files[0] = 0;
  create->token = 0;
  create->disposition = closing;
  create->phase = SYSFILE_CREATE_RESOLVED;
  return 0;
}
isize
sysfilecreaterevokeprepare(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                           Handle call, u64 deadline, Connclaim *claim, Handle *borrowed_call)
{
  Createop  *create;
  Connpeer   peer;
  Connclaim  prepared;
  Bootstrap *boot;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  boot = &work->state.bootstrap;
  if (!external(pool, work, cache, claim, sizeof(*claim)) ||
      !external(pool, work, cache, borrowed_call, sizeof(*borrowed_call)) ||
      !separate(claim, sizeof(*claim), borrowed_call, sizeof(*borrowed_call)) ||
      !separate(key, sizeof(*key), claim, sizeof(*claim)) ||
      !separate(key, sizeof(*key), borrowed_call, sizeof(*borrowed_call)))
    return -ERR_INVALID;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (error)
    return error;
  if (sysbootcheck(boot))
    return -ERR_STATE;
  if (boot->state != SYSBOOT_FREE)
    return -ERR_BUSY;
  if (!call)
    call = create->call;
  error = sysconnpeer(call, &peer);
  if (error)
    return error;
  if (!samepeer(peer, create->peer))
    return -ERR_DENIED;
  error = sysconnabortbegin(cache, create->files[0], &prepared);
  if (!error)
    error = sysbootprepare(boot, prepared, FS_ABORT, create->token, deadline);
  if (error)
    return error;
  copy(claim, &prepared, sizeof(prepared));
  *borrowed_call = call;
  return 0;
}
isize
sysfilecreaterevoked(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                     Connclaim claim)
{
  Createop  *create;
  Bootstrap *boot;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (create->phase == SYSFILE_CREATE_RESOLVED)
    return create->peer.pid && create->disposition == SYSFILE_RESULT_REVOKED && !create->files[0]
               ? 0
               : -ERR_STATE;
  error = resource(create, &work->state.operations[key->lease.slot]);
  if (error)
    return error;
  boot = &work->state.bootstrap;
  if (claim.tag.generation || claim.tag.index != SYSCONN_COUNT ||
      !bootmatches(boot, claim, create, FS_ABORT))
    return -ERR_STALE;
  error = sysbootabortreply(boot, claim);
  if (error)
    return error;
  if (create->call) {
    error = __sys_free(create->call);
    if (error)
      return error;
    create->call = 0;
  }
  error = sysfilerefabort(pool, work->state.self, &create->stage);
  if (error)
    return error;
  error = sysbootfinish(boot, claim);
  if (error)
    return error;
  create->files[0] = create->files[1] = 0;
  create->token = 0;
  create->disposition = SYSFILE_RESULT_REVOKED;
  create->phase = SYSFILE_CREATE_RESOLVED;
  return 0;
}
isize
sysfilecreateended(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Createop  *create;
  Slotop    *op;
  Bootstrap *boot;
  Connclaim  claim;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!(create->flags & SYSFILE_CREATE_PEER_ENDED))
    return -ERR_STATE;
  if (create->phase == SYSFILE_CREATE_RESOLVED)
    return create->disposition == SYSFILE_RESULT_ENDED ? 0 : -ERR_STATE;
  op = &work->state.operations[key->lease.slot];
  boot = &work->state.bootstrap;
  if (create->phase != SYSFILE_CREATE_CAPTURED || create->error || create->count != 1 ||
      op->phase != SYSOP_ACKED || op->opcode != FS_OPEN || op->kind != IPC_RESOURCE_REPLY ||
      !create->token || sysbootcheck(boot) || boot->opcode != FS_ABORT ||
      boot->token != create->token || boot->claim.owner != create->files[0] ||
      boot->claim.tag.generation || boot->claim.tag.index != SYSCONN_COUNT)
    return -ERR_STATE;
  if (boot->state != SYSBOOT_PREPARED) {
    u32 i;
    if ((boot->state != SYSBOOT_RESULT && boot->state != SYSBOOT_ACKED) ||
        boot->status != -ERR_PEER || boot->kind != IPC_NONE || boot->flags || boot->resource)
      return -ERR_STATE;
    for (i = 0; i < 4; i++)
      if (boot->words[i])
        return -ERR_STATE;
    error = sysbootfinished(boot, boot->claim);
    if (!error && boot->state == SYSBOOT_RESULT)
      error = sysbootack(boot, boot->claim);
    if (error)
      return error;
  }
  /* The bound continuation is finished; any submitted RPC drained and ACKed
     above. Only the creator-owned CALL is freed, never a cached borrowed one. */
  if (create->call) {
    error = __sys_free(create->call);
    if (error)
      return error;
    create->call = 0;
  }
  copy(&claim, &boot->claim, sizeof(claim));
  error = sysfilerefabort(pool, work->state.self, &create->stage);
  if (!error)
    error = boot->state == SYSBOOT_PREPARED ? sysbootunprepare(boot, claim)
                                            : sysbootfinish(boot, claim);
  if (error)
    return error;
  create->files[0] = create->files[1] = 0;
  create->token = 0;
  create->disposition = SYSFILE_RESULT_ENDED;
  create->phase = SYSFILE_CREATE_RESOLVED;
  return 0;
}
isize
sysfilecreatebackendended(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                          Connwork *backend, Connclaim claim)
{
  Createop  *create;
  Slotop    *op;
  Bootstrap *boot;
  Connentry  entry;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (create->phase == SYSFILE_CREATE_RESOLVED)
    return create->flags & SYSFILE_CREATE_PEER_ENDED && create->disposition == SYSFILE_RESULT_ENDED
               ? 0
               : -ERR_STATE;
  op = &work->state.operations[key->lease.slot];
  if (create->phase != SYSFILE_CREATE_CAPTURED || create->error || create->count != 1 ||
      !create->token || op->phase != SYSOP_ACKED || op->opcode != FS_OPEN ||
      op->kind != IPC_RESOURCE_REPLY)
    return -ERR_STATE;
  if (!backend || !separate(backend, sizeof(*backend), key, sizeof(*key)))
    return -ERR_INVALID;
  error = backendentry(pool, work, cache, create, backend, &entry);
  if (error)
    return error;
  boot = &backend->state.bootstrap;
  if (backend->state.ended != 1 || entry.state != SYSCONN_DRAINING || sysbootcheck(boot) ||
      boot->state == SYSBOOT_FREE || boot->opcode != FS_ATTACH)
    return -ERR_STATE;
  if (claim.tag.reserved || claim.tag.generation != backend->state.tag.generation ||
      claim.tag.index != backend->state.tag.index || claim.seq != entry.sequence ||
      claim.owner != entry.owner || claim.owner != create->files[0] ||
      !sameclaim(boot->claim, claim) || boot->token != create->token)
    return -ERR_STALE;
  if (create->call && (create->call == backend->state.call || create->call == work->state.call))
    return -ERR_STATE;
  if (backend != work && work->state.bootstrap.state != SYSBOOT_FREE &&
      work->state.bootstrap.opcode == FS_ABORT && work->state.bootstrap.token == create->token &&
      work->state.bootstrap.claim.owner == create->files[0])
    return -ERR_BUSY;
  /* The exact old backend ended, so this reference cannot be published or
     retried. Its ATTACH IO must nevertheless drain before local retirement. */
  create->flags |= SYSFILE_CREATE_PEER_ENDED;
  if (boot->state == SYSBOOT_WAITING)
    return -ERR_WOULD_BLOCK;
  if (boot->state != SYSBOOT_PREPARED) {
    error = sysbootfinished(boot, claim);
    if (!error && boot->state == SYSBOOT_RESULT)
      error = sysbootack(boot, claim);
    if (error)
      return error;
  }
  if (create->call) {
    error = __sys_free(create->call);
    if (error)
      return error;
    create->call = 0;
  }
  error = sysfilerefabort(pool, work->state.self, &create->stage);
  if (error)
    return error;
  create->files[0] = create->files[1] = 0;
  create->token = 0;
  create->disposition = SYSFILE_RESULT_ENDED;
  create->phase = SYSFILE_CREATE_RESOLVED;
  return 0;
}
isize
sysfilecreateattachdiscard(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                           Connwork *backend, Connclaim claim, Handle space, Bootmap *map)
{
  Createop  *create;
  Connentry  entry;
  Slotop    *op;
  Bootstrap *boot;
  isize      error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!backend || !external(pool, work, cache, map, sizeof(*map)) ||
      !separate(backend, sizeof(*backend), key, sizeof(*key)) ||
      !separate(backend, sizeof(*backend), map, sizeof(*map)) ||
      !separate(key, sizeof(*key), map, sizeof(*map)))
    return -ERR_INVALID;
  op = &work->state.operations[key->lease.slot];
  if (op->opcode != FS_OPEN || op->kind != IPC_RESOURCE_REPLY || op->phase != SYSOP_ACKED ||
      create->phase != SYSFILE_CREATE_RESOLVED ||
      (create->disposition != SYSFILE_RESULT_REVOKED &&
       create->disposition != SYSFILE_RESULT_ENDED))
    return -ERR_STATE;
  error = backendentry(pool, work, cache, create, backend, &entry);
  if (error)
    return error;
  boot = &backend->state.bootstrap;
  if (boot->opcode != FS_ATTACH || !sameclaim(boot->claim, claim) || claim.owner != op->file)
    return -ERR_STALE;
  if (entry.connid) {
    if (map->mapped || map->verified || map->address || map->bytes || map->connid ||
        map->serverimage)
      return -ERR_INVALID;
    return entry.state == SYSCONN_READY ? sysconnworkreuseabort(backend, cache, claim)
                                        : sysconnworkbootended(backend, cache, claim);
  }
  return sysconnworkdiscard(backend, cache, claim, space, map);
}

isize
sysfilepipeinstall(Fdtable *table, const Fdtag tags[2], const u64 files[2], int *userfds)
{
  int   numbers[2];
  isize error = sysfdcheckpair(table, tags);
  if (error)
    return error;
  if (!files || !separate(files, 2 * sizeof(*files), table, sizeof(*table)) || !files[0] ||
      !files[1] || files[0] == files[1])
    return -ERR_INVALID;
  if (!userfds || !separate(userfds, sizeof(numbers), table, sizeof(*table)) ||
      !separate(userfds, sizeof(numbers), tags, 2 * sizeof(*tags)) ||
      !separate(userfds, sizeof(numbers), files, 2 * sizeof(*files)))
    return -ERR_FAULT;
  numbers[0] = (int) tags[0].index;
  numbers[1] = (int) tags[1].index;
  error = __sys_copy(userfds, numbers, sizeof(numbers));
  return error ? error : sysfdpublishpair(table, tags, files, 0);
}
isize
sysfilecreatefinish(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                    u64 files[2], u64 *source)
{
  Createop *create;
  Slotop   *op;
  u64       carrier, original;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, files, 2 * sizeof(u64)) ||
      !external(pool, work, cache, source, sizeof(*source)) || *source || files[0] || files[1] ||
      !separate(files, 2 * sizeof(u64), source, sizeof(*source)) ||
      !separate(key, sizeof(*key), files, 2 * sizeof(u64)) ||
      !separate(key, sizeof(*key), source, sizeof(*source)))
    return -ERR_INVALID;
  op = &work->state.operations[key->lease.slot];
  if (op->phase != SYSOP_ACKED || create->phase != SYSFILE_CREATE_RESOLVED || create->call ||
      create->token)
    return -ERR_BUSY;
  original = create->source;
  op->flags &= ~SYSOP_CREATOR;
  error = sysopfinish(work, cache, key, &carrier);
  if (error) {
    op->flags |= SYSOP_CREATOR;
    return error;
  }
  files[0] = create->files[0];
  files[1] = create->files[1];
  *source = original;
  clear(create, sizeof(*create));
  return 0;
}

static int
openstorage(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Opencreate *open)
{
  return external(pool, work, cache, table, sizeof(*table)) &&
         external(pool, work, cache, open, sizeof(*open)) &&
         separate(table, sizeof(*table), open, sizeof(*open));
}

isize
sysfileopenprepare(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, u32 layer,
                   u64 owner, const u64 args[8], const void *path, u64 bytes, u64 fdflags,
                   Opencreate *open, Msg *message)
{
  u64   source = 0;
  isize error;
  if (!openstorage(pool, work, cache, table, open) || open->operation.lease.seq ||
      (fdflags & ~((u64) BOOT_FD_CLOEXEC)) ||
      !separate(message, sizeof(*message), table, sizeof(*table)) ||
      !separate(message, sizeof(*message), open, sizeof(*open)) ||
      !separate(args, 8 * sizeof(u64), table, sizeof(*table)) ||
      !separate(args, 8 * sizeof(u64), open, sizeof(*open)) ||
      !separate(path, bytes, table, sizeof(*table)) || !separate(path, bytes, open, sizeof(*open)))
    return -ERR_INVALID;
  error = sysfdreserve(table, 0, &open->tag);
  if (error)
    return error;
  error = sysfilecreateprepare(pool, work, cache, layer, owner, FS_OPEN, 0, &source, args, path,
                               bytes, &open->operation, message);
  if (error) {
    (void) sysfdcancel(table, open->tag);
    clear(open, sizeof(*open));
  } else
    open->flags = fdflags;
  return error;
}

static isize
opencheck(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Opencreate *open,
          Createop **create)
{
  const Fdentry *fd;
  isize          error;
  if (!openstorage(pool, work, cache, table, open) || open->tag.index >= BOOT_FD_COUNT ||
      open->tag.state != SYSFD_RESERVED || (open->flags & ~((u64) BOOT_FD_CLOEXEC)))
    return -ERR_INVALID;
  fd = &table->entries[open->tag.index];
  if (fd->state != SYSFD_RESERVED || fd->generation != open->tag.generation)
    return -ERR_STALE;
  error = target(pool, work, cache, &open->operation, create);
  if (error)
    return error;
  return work->state.operations[open->operation.lease.slot].opcode == FS_OPEN && !(*create)->source
             ? 0
             : -ERR_INVALID;
}

isize
sysfileopencheck(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Opencreate *open)
{
  Createop *create;
  return opencheck(pool, work, cache, table, open, &create);
}

isize
sysfileopeninterrupt(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                     Opencreate *open, u64 now)
{
  Createop *create;
  isize     error = opencheck(pool, work, cache, table, open, &create);
  if (error)
    return error;
  return sysopinterrupt(work, cache, &open->operation, now);
}

isize
sysfileopenabort(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Opencreate *open)
{
  Createop *create;
  u64       source = 0;
  isize     error = opencheck(pool, work, cache, table, open, &create);
  if (!error)
    error = sysfilecreateabort(pool, work, cache, &open->operation, &source);
  if (!error) {
    error = sysfdcancel(table, open->tag);
    clear(open, sizeof(*open));
  }
  return error;
}

isize
sysfileopendiscard(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                   Opencreate *open)
{
  Createop *create;
  u64       source = 0, files[2] = {0};
  isize     error = opencheck(pool, work, cache, table, open, &create);
  if (error)
    return error;
  if (create->phase != SYSFILE_CREATE_RESOLVED || create->files[0] || create->files[1] ||
      (create->disposition != SYSFILE_RESULT_REVOKED &&
       create->disposition != SYSFILE_RESULT_ENDED))
    return -ERR_STATE;
  error = sysfilecreatefinish(pool, work, cache, &open->operation, files, &source);
  if (!error) {
    error = sysfdcancel(table, open->tag);
    clear(open, sizeof(*open));
  }
  return error;
}

isize
sysfileopenfinish(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                  Opencreate *open, isize *business)
{
  Createop *create;
  u64       source = 0, files[2] = {0};
  isize     status, native, number, cleanup, error;
  error = opencheck(pool, work, cache, table, open, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, business, sizeof(*business)) ||
      !separate(table, sizeof(*table), business, sizeof(*business)) ||
      !separate(open, sizeof(*open), business, sizeof(*business)))
    return -ERR_INVALID;
  status = create->error;
  native = work->state.operations[open->operation.lease.slot].status;
  if (!status && !native && !create->files[0])
    return -ERR_STATE;
  error = sysfilecreatefinish(pool, work, cache, &open->operation, files, &source);
  if (error)
    return error;
  number = open->tag.index;
  if (files[0])
    error = sysfdpublish(table, open->tag, files[0], open->flags);
  if (!files[0] || error) {
    cleanup = sysfdcancel(table, open->tag);
    if (files[0]) {
      isize drop = sysfilerefdrop(pool, work->state.self, files[0]);
      if (!cleanup)
        cleanup = drop;
    }
    if (cleanup)
      error = cleanup;
  }
  *business = native ? 0 : status;
  clear(open, sizeof(*open));
  return error ? error : native ? native : status ? 0 : number;
}

static isize
openack(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Slotop *op;
  isize   error = sysoppoll(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (op->status == -ERR_PEER) {
    error = work->state.ended ? 0 : sysoppeerended(work, cache, key);
    if (!error)
      error = sysfilecreatefailed(pool, work, cache, key);
  }
  return error ? error : sysfilecreateack(pool, work, cache, key);
}

isize
sysfileopenabandon(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                   Opencreate *open)
{
  Createop *create;
  Slotop   *op;
  Opkey     background;
  isize     error = opencheck(pool, work, cache, table, open, &create);
  if (error)
    return error;
  op = &work->state.operations[open->operation.lease.slot];
  if (!(op->flags & SYSOP_CANCEL_SENT) || op->phase == SYSOP_ACKED ||
      create->phase == SYSFILE_CREATE_RESOLVED)
    return -ERR_STATE;
  error = sysopabandon(work, cache, &open->operation, &background);
  if (!error) {
    error = sysfdcancel(table, open->tag);
    clear(open, sizeof(*open));
  }
  return error;
}

static isize
openaborttoken(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key, Createop *create)
{
  Bootstrap *boot = &work->state.bootstrap;
  Connclaim  claim;
  Connpeer   peer;
  Handle     call = create->call;
  Msg        message;
  Ipcmsg     result;
  isize      error;
  if (boot->state == SYSBOOT_FREE) {
    error = sysfilecreaterevokeprepare(pool, work, cache, key, call, 0, &claim, &call);
    if (error)
      return error;
  } else {
    claim = boot->claim;
    if (sysbootcheck(boot) || boot->opcode != FS_ABORT || boot->token != create->token ||
        claim.owner != create->files[0] || claim.tag.generation || claim.tag.index != SYSCONN_COUNT)
      return -ERR_BUSY;
  }
  if (create->flags & SYSFILE_CREATE_PEER_ENDED)
    return sysfilecreateended(pool, work, cache, key);
  if (boot->state == SYSBOOT_PREPARED) {
    error = sysconnpeer(call, &peer);
    if (error || !samepeer(peer, create->peer))
      return error ? error : -ERR_DENIED;
    error = sysbootrequest(boot, claim, &message);
    if (error)
      return error;
    error = ipc_call(call, &message, CALL_ASYNC | CALL_CONTROL, &result);
    if (error == -ERR_PEER) {
      create->flags |= SYSFILE_CREATE_PEER_ENDED;
      return sysfilecreateended(pool, work, cache, key);
    }
    if (error)
      return error;
    error = sysbootsubmitted(boot, claim, &result.request);
    if (error)
      return error;
  }
  if (boot->state == SYSBOOT_WAITING) {
    error = sysbootpoll(boot, claim);
    if (error)
      return error;
  }
  if (boot->status == -ERR_PEER) {
    create->flags |= SYSFILE_CREATE_PEER_ENDED;
    return sysfilecreateended(pool, work, cache, key);
  }
  error = sysbootfinished(boot, claim);
  if (!error)
    error = sysbootabortreply(boot, claim);
  if (!error && boot->state == SYSBOOT_RESULT)
    error = sysbootack(boot, claim);
  return error ? error : sysfilecreaterevoked(pool, work, cache, key, claim);
}

isize
sysfileopendrain(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key)
{
  Createop *create;
  Slotop   *op;
  u64       files[2] = {0}, source = 0;
  isize     error = target(pool, work, cache, key, &create);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (op->opcode != FS_OPEN || key->owner ||
      (op->flags & (SYSOP_BACKGROUND | SYSOP_APP_ENDED)) != (SYSOP_BACKGROUND | SYSOP_APP_ENDED))
    return -ERR_STATE;
  error = openack(pool, work, cache, key);
  if (error)
    return error;
  if (create->token)
    error = openaborttoken(pool, work, cache, key, create);
  else if (create->phase != SYSFILE_CREATE_RESOLVED)
    error = sysfilecreatesettle(pool, work, cache, key, 1);
  return error ? error : sysfilecreatefinish(pool, work, cache, key, files, &source);
}

isize
sysfileopencollect(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                   Opencreate *open, int *fd, isize *business)
{
  Createop    *create;
  const Opkey *key;
  isize        error = opencheck(pool, work, cache, table, open, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, fd, sizeof(*fd)) ||
      !external(pool, work, cache, business, sizeof(*business)) ||
      !separate(fd, sizeof(*fd), business, sizeof(*business)) ||
      !separate(fd, sizeof(*fd), table, sizeof(*table)) ||
      !separate(fd, sizeof(*fd), open, sizeof(*open)) ||
      !separate(business, sizeof(*business), table, sizeof(*table)) ||
      !separate(business, sizeof(*business), open, sizeof(*open)))
    return -ERR_INVALID;
  key = &open->operation;
  error = openack(pool, work, cache, key);
  if (error)
    return error;
  if (create->token)
    return 1;
  error = sysfilecreatesettle(pool, work, cache, key, 0);
  if (!error)
    error = sysfileopenfinish(pool, work, cache, table, open, business);
  if (error < 0)
    return error;
  *fd = *business ? -1 : (int) error;
  return 0;
}

static int
pipestorage(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Pipecreate *pipe)
{
  return external(pool, work, cache, table, sizeof(*table)) &&
         external(pool, work, cache, pipe, sizeof(*pipe)) &&
         separate(table, sizeof(*table), pipe, sizeof(*pipe));
}

isize
sysfilepipeprepare(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, u32 layer,
                   u64 owner, Pipecreate *pipe, Msg *message)
{
  const u64 args[8] = {0, 8192};
  u64       source = 0;
  isize     error;
  if (!pipestorage(pool, work, cache, table, pipe) || pipe->operation.lease.seq ||
      !separate(message, sizeof(*message), table, sizeof(*table)) ||
      !separate(message, sizeof(*message), pipe, sizeof(*pipe)))
    return -ERR_INVALID;
  error = sysfdreservepair(table, pipe->tags);
  if (error)
    return error;
  error = sysfilecreateprepare(pool, work, cache, layer, owner, FS_PIPE, 0, &source, args, NULL, 0,
                               &pipe->operation, message);
  if (error) {
    (void) sysfdcancelpair(table, pipe->tags);
    clear(pipe, sizeof(*pipe));
  }
  return error;
}

static isize
pipecheck(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Pipecreate *pipe,
          Createop **create)
{
  isize error;
  if (!pipestorage(pool, work, cache, table, pipe))
    return -ERR_INVALID;
  error = sysfdcheckpair(table, pipe->tags);
  if (!error)
    error = target(pool, work, cache, &pipe->operation, create);
  if (!error && work->state.operations[pipe->operation.lease.slot].opcode != FS_PIPE)
    return -ERR_INVALID;
  return error;
}

isize
sysfilepipeabort(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table, Pipecreate *pipe)
{
  Createop *create;
  u64       source = 0;
  isize     error = pipecheck(pool, work, cache, table, pipe, &create);
  if (!error)
    error = sysfilecreateabort(pool, work, cache, &pipe->operation, &source);
  if (!error) {
    error = sysfdcancelpair(table, pipe->tags);
    clear(pipe, sizeof(*pipe));
  }
  return error;
}

isize
sysfilepipefinish(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                  Pipecreate *pipe, int *userfds, isize *business)
{
  Createop *create;
  u64       source = 0, files[2] = {0};
  isize     status, native, cleanup, error = pipecheck(pool, work, cache, table, pipe, &create);
  if (error)
    return error;
  if (!external(pool, work, cache, business, sizeof(*business)) ||
      !separate(table, sizeof(*table), business, sizeof(*business)) ||
      !separate(pipe, sizeof(*pipe), business, sizeof(*business)))
    return -ERR_INVALID;
  if (!external(pool, work, cache, userfds, 2 * sizeof(*userfds)) ||
      !separate(pipe, sizeof(*pipe), userfds, 2 * sizeof(*userfds)) ||
      !separate(business, sizeof(*business), userfds, 2 * sizeof(*userfds)))
    userfds = NULL;
  if (work->state.operations[pipe->operation.lease.slot].status == -ERR_PEER) {
    if (!work->state.ended)
      error = sysoppeerended(work, cache, &pipe->operation);
    if (!error)
      error = sysfilecreatefailed(pool, work, cache, &pipe->operation);
  }
  if (!error)
    error = sysfilecreateack(pool, work, cache, &pipe->operation);
  if (!error)
    error = sysfilecreatesettle(pool, work, cache, &pipe->operation, 0);
  if (error)
    return error;
  status = create->error;
  native = work->state.operations[pipe->operation.lease.slot].status;
  error = sysfilecreatefinish(pool, work, cache, &pipe->operation, files, &source);
  if (error)
    return error;
  if (!status)
    error = sysfilepipeinstall(table, pipe->tags, files, userfds);
  if (status || error) {
    cleanup = sysfdcancelpair(table, pipe->tags);
    if (!status) {
      isize first = sysfilerefdrop(pool, work->state.self, files[0]);
      isize second = sysfilerefdrop(pool, work->state.self, files[1]);
      if (!cleanup)
        cleanup = first ? first : second;
    }
    if (cleanup)
      error = cleanup;
  }
  *business = native ? 0 : status;
  clear(pipe, sizeof(*pipe));
  return error ? error : native;
}
