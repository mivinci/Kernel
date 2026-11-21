#include "private/argument.h"
#include "private/boot.h"
#include "private/syscall.h"
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
sysargboot(const Argsnap *snapshot, Boot *boot, u64 capacity)
{
  u64         offset[4], length[4], base, end, used, bytes, i, j;
  const char *text;
  Strref      ref;
  u8         *destination;
  isize       error;
  if (capacity > BOOT_MAX_BYTES)
    capacity = BOOT_MAX_BYTES;
  if (!boot || ((u64) (uintptr_t) boot & 7) || capacity < sizeof(*boot) ||
      !separate(boot, capacity, snapshot, sizeof(*snapshot)))
    return -ERR_INVALID;
  if (!snapshot->data || snapshot->argc > BOOT_ARG_COUNT || snapshot->envc > BOOT_ARG_COUNT ||
      snapshot->bytes > BOOT_ARG_BYTES ||
      snapshot->bytes < (u64) (snapshot->argc + snapshot->envc + 2) * sizeof(char *) ||
      snapshot->argv_off ||
      snapshot->env_off != (snapshot->envc ? snapshot->argc * sizeof(Strref) : 0) ||
      !separate(boot, capacity, snapshot->data, snapshot->bytes))
    return -ERR_INVALID;
  if (boot->magic != BOOT_MAGIC || boot->format != BOOT_FORMAT || boot->flags ||
      boot->user_lo != BOOT_USER_LO || boot->user_hi != BOOT_USER_HI ||
      boot->bytes < sizeof(*boot) || boot->bytes > capacity || boot->argc || boot->envc ||
      boot->argv_off || boot->env_off || boot->resource_count > BOOT_RESOURCE_COUNT ||
      boot->fd_count > BOOT_FD_COUNT)
    return -ERR_INVALID;
  if ((boot->package_va || boot->package_bytes) &&
      (!boot->package_bytes || boot->package_va < BOOT_PACKAGE_LO ||
       boot->package_va >= BOOT_PACKAGE_HI ||
       boot->package_bytes > BOOT_PACKAGE_HI - boot->package_va))
    return -ERR_INVALID;
  offset[0] = 0;
  length[0] = sizeof(*boot);
  offset[1] = boot->resources_off;
  length[1] = boot->resource_count * sizeof(Resentry);
  offset[2] = boot->fds_off;
  length[2] = boot->fd_count * sizeof(Bootfd);
  offset[3] = boot->fix_off;
  length[3] = boot->fix_bytes;
  for (i = 1; i < 4; i++) {
    if ((offset[i] & 7) || offset[i] > boot->bytes || length[i] > boot->bytes - offset[i])
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (length[i] && length[j] && offset[i] < offset[j] + length[j] &&
          offset[j] < offset[i] + length[i])
        return -ERR_INVALID;
  }
  if (boot->fd_count &&
      !sysbootfds((const Bootfd *) ((const u8 *) boot + boot->fds_off), boot->fd_count))
    return -ERR_INVALID;
  used = (u64) (snapshot->argc + snapshot->envc) * sizeof(Strref);
  for (i = 0; i < snapshot->argc + snapshot->envc; i++) {
    error = sysargat(snapshot, i >= snapshot->argc, i < snapshot->argc ? i : i - snapshot->argc,
                     &text, &bytes);
    if (error || text != (const char *) snapshot->data + used)
      return -ERR_INVALID;
    used += bytes;
  }
  if (used + 2 * sizeof(char *) != snapshot->bytes)
    return -ERR_INVALID;
  for (i = used; i < snapshot->bytes; i++)
    if (snapshot->data[i])
      return -ERR_INVALID;
  base = ALIGN_UP(boot->bytes, 8);
  if (base > capacity || snapshot->bytes > capacity - base)
    return -ERR_CAPACITY;
  end = base + snapshot->bytes;
  destination = (u8 *) boot;
  for (i = boot->bytes; i < base; i++)
    destination[i] = 0;
  for (i = 0; i < snapshot->bytes; i++)
    destination[base + i] = snapshot->data[i];
  for (i = 0; i < snapshot->argc + snapshot->envc; i++) {
    for (j = 0; j < sizeof(ref); j++)
      ((u8 *) &ref)[j] = snapshot->data[i * sizeof(ref) + j];
    ref.offset += (u32) base;
    for (j = 0; j < sizeof(ref); j++)
      destination[base + i * sizeof(ref) + j] = ((const u8 *) &ref)[j];
  }
  boot->argc = snapshot->argc;
  boot->envc = snapshot->envc;
  boot->argv_off = snapshot->argc ? base : 0;
  boot->env_off = snapshot->envc ? base + snapshot->env_off : 0;
  boot->bytes = end;
  return 0;
}
