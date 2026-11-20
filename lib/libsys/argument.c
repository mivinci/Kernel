#include "private/context.h"
typedef struct Argdest Argdest;
struct Argdest
{
  u8   *data;
  u64   capacity;
  u64   limit;
  void *metadata;
  u64   metadata_bytes;
};
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static int
sourceok(const Argdest *work, u64 address, u64 bytes)
{
  const void *source = (const void *) (uintptr_t) address;
  return separate(source, bytes, work->data, work->capacity) &&
         separate(source, bytes, work->metadata, work->metadata_bytes);
}
static isize
string(Argdest *work, u64 at, u64 limit, u64 address, u64 *length)
{
  u8    block[64];
  u64   used = 0, chunk, take, i;
  isize error;
  if (!address)
    return -ERR_FAULT;
  while (used < limit) {
    chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
    if (chunk > sizeof(block))
      chunk = sizeof(block);
    if (chunk > limit - used)
      chunk = limit - used;
    if (address > UINT64_MAX - chunk)
      return -ERR_FAULT;
    error = __sys_copy(block, (const void *) (uintptr_t) address, chunk);
    if (error)
      return error;
    for (take = 0; take < chunk && block[take]; take++) {
    }
    if (take < chunk)
      take++;
    if (!sourceok(work, address, take))
      return -ERR_INVALID;
    for (i = 0; i < take; i++)
      work->data[at + used + i] = block[i];
    used += take;
    if (!block[take - 1]) {
      *length = used;
      return 0;
    }
    address += chunk;
  }
  return -ERR_LIMIT;
}
isize
sysargstring(void *data, u64 capacity, const char *source, u64 *bytes)
{
  Argdest work;
  u64     length;
  isize   error;
  if (capacity > BOOT_ARG_BYTES)
    return -ERR_LIMIT;
  if (!separate(data, capacity, bytes, sizeof(*bytes)))
    return -ERR_INVALID;
  work.data = data;
  work.capacity = capacity;
  work.limit = capacity;
  work.metadata = bytes;
  work.metadata_bytes = sizeof(*bytes);
  error = string(&work, 0, capacity, (u64) (uintptr_t) source, &length);
  if (!error)
    *bytes = length;
  return error;
}
static isize
vector(Argdest *work, const char *const *source, u32 start, u32 *count)
{
  u64   address = (u64) (uintptr_t) source, pointer, offset;
  u32   i, j;
  isize error;
  if (!source) {
    *count = 0;
    return 0;
  }
  for (i = 0; i <= BOOT_ARG_COUNT; i++) {
    offset = (u64) i * sizeof(pointer);
    if (address > UINT64_MAX - offset || address + offset > UINT64_MAX - sizeof(pointer))
      return -ERR_FAULT;
    if (!sourceok(work, address + offset, sizeof(pointer)))
      return -ERR_INVALID;
    error = __sys_copy(&pointer, (const void *) (uintptr_t) (address + offset), sizeof(pointer));
    if (error)
      return error;
    if (!pointer) {
      *count = i;
      return 0;
    }
    if (i == BOOT_ARG_COUNT)
      return -ERR_LIMIT;
    if ((u64) (start + i + 1) * sizeof(pointer) > work->limit)
      return -ERR_CAPACITY;
    for (j = 0; j < sizeof(pointer); j++)
      work->data[(start + i) * sizeof(pointer) + j] = ((const u8 *) &pointer)[j];
  }
  return -ERR_STATE;
}
isize
sysargcopy(void *data, u64 capacity, const char *const *argv, const char *const *envp, Argsnap *out)
{
  Argdest work;
  u32     argc, envc, i, j;
  u64     used, pointer, length, limit;
  Strref  ref;
  isize   error;
  if (capacity > BOOT_ARG_BYTES)
    capacity = BOOT_ARG_BYTES;
  if (capacity < 2 * sizeof(char *))
    return -ERR_CAPACITY;
  limit = capacity - 2 * sizeof(char *);
  if (((u64) (uintptr_t) data & 7) || !separate(data, capacity, out, sizeof(*out)))
    return -ERR_INVALID;
  work.data = data;
  work.capacity = capacity;
  work.limit = limit;
  work.metadata = out;
  work.metadata_bytes = sizeof(*out);
  error = vector(&work, argv, 0, &argc);
  if (!error)
    error = vector(&work, envp, argc, &envc);
  if (error)
    return error;
  used = (u64) (argc + envc) * sizeof(Strref);
  for (i = 0; i < argc + envc; i++) {
    for (j = 0; j < sizeof(pointer); j++)
      ((u8 *) &pointer)[j] = work.data[i * sizeof(pointer) + j];
    error = string(&work, used, limit - used, pointer, &length);
    if (error)
      return error == -ERR_LIMIT && capacity < BOOT_ARG_BYTES ? -ERR_CAPACITY : error;
    ref.offset = (u32) used;
    ref.length = (u32) length;
    for (j = 0; j < sizeof(ref); j++)
      work.data[i * sizeof(ref) + j] = ((const u8 *) &ref)[j];
    used += length;
  }
  for (j = 0; j < 2 * sizeof(char *); j++)
    work.data[used + j] = 0;
  out->data = data;
  out->bytes = used + 2 * sizeof(char *);
  out->argc = argc;
  out->envc = envc;
  out->argv_off = 0;
  out->env_off = envc ? argc * sizeof(Strref) : 0;
  return 0;
}
isize
sysargat(const Argsnap *snapshot, u32 environment, u64 index, const char **text, u64 *bytes)
{
  Strref ref;
  u64    count, offset, table, i;
  if (!snapshot || !snapshot->data || environment > 1 || snapshot->argc > BOOT_ARG_COUNT ||
      snapshot->envc > BOOT_ARG_COUNT || snapshot->bytes > BOOT_ARG_BYTES || snapshot->argv_off ||
      snapshot->env_off != (snapshot->envc ? snapshot->argc * sizeof(Strref) : 0))
    return -ERR_INVALID;
  table = (u64) (snapshot->argc + snapshot->envc) * sizeof(Strref);
  if (snapshot->bytes < table + 2 * sizeof(char *) ||
      !separate(snapshot, sizeof(*snapshot), text, sizeof(*text)) ||
      !separate(snapshot, sizeof(*snapshot), bytes, sizeof(*bytes)) ||
      !separate(snapshot->data, snapshot->bytes, text, sizeof(*text)) ||
      !separate(snapshot->data, snapshot->bytes, bytes, sizeof(*bytes)) ||
      !separate(text, sizeof(*text), bytes, sizeof(*bytes)))
    return -ERR_INVALID;
  count = environment ? snapshot->envc : snapshot->argc;
  if (index >= count)
    return -ERR_INVALID;
  offset = (environment ? snapshot->env_off : 0) + index * sizeof(Strref);
  for (i = 0; i < sizeof(ref); i++)
    ((u8 *) &ref)[i] = snapshot->data[offset + i];
  if (!ref.length || ref.offset < table || ref.offset > snapshot->bytes - 2 * sizeof(char *) ||
      ref.length > snapshot->bytes - 2 * sizeof(char *) - ref.offset ||
      snapshot->data[ref.offset + ref.length - 1])
    return -ERR_INVALID;
  for (i = 0; i + 1 < ref.length; i++)
    if (!snapshot->data[ref.offset + i])
      return -ERR_INVALID;
  *text = (const char *) snapshot->data + ref.offset;
  *bytes = ref.length;
  return 0;
}
isize
sysctxarguments(Rbank *bank, Connpeer self, Rkey key, const char *const *argv,
                const char *const *envp, Argsnap *out)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_PREPARED || context->input_saved)
    return -ERR_STATE;
  if (!separate(bank, sizeof(*bank), out, sizeof(*out)))
    return -ERR_INVALID;
  error = sysargcopy(bank->snapshots[key.layer], RCTX_SNAPSHOT_BYTES, argv, envp, out);
  if (error)
    return error;
  context->input_bytes = out->bytes;
  context->input_kind = 1;
  context->input_argc = out->argc;
  context->input_envc = out->envc;
  context->input_saved = 1;
  return 0;
}
isize
sysctxargumentview(Rbank *bank, Connpeer self, Rkey key, Argsnap *out)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), out, sizeof(*out)))
    return -ERR_INVALID;
  if (!context->input_saved || context->input_kind != 1)
    return -ERR_STATE;
  out->data = bank->snapshots[key.layer];
  out->bytes = context->input_bytes;
  out->argc = context->input_argc;
  out->envc = context->input_envc;
  out->argv_off = 0;
  out->env_off = out->envc ? out->argc * sizeof(Strref) : 0;
  return 0;
}
