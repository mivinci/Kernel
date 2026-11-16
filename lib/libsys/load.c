#include "private/elf.h"
static isize
mapping(Elfwork *work, Handle space, u64 address, u64 bytes, u64 prot)
{
  Maparg arg;
  arg.mem = work->memory;
  arg.offset = 0;
  arg.address = address;
  arg.length = bytes;
  arg.prot = prot;
  arg.reserved = 0;
  return __sys_map(space, &arg);
}
static isize
allocate(Elfwork *work, u64 bytes, u64 kind)
{
  Allocarg arg;
  arg.bytes = bytes;
  arg.kind = kind;
  arg.align = arg.minaddr = arg.maxaddr = arg.reserved = 0;
  return __sys_alloc(work->view, &arg, &work->memory);
}
static isize
alias(Elfwork *work, u64 bytes)
{
  isize error = mapping(work, work->parent_space, work->scratch, bytes, MAP_READ | MAP_WRITE);
  if (!error)
    work->mapped = 1;
  return error;
}
static isize
unalias(Elfwork *work)
{
  isize error;
  if (!work->mapped)
    return 0;
  error = __sys_unmap(work->parent_space, work->scratch);
  if (!error)
    work->mapped = 0;
  return error;
}
isize
elfclean(Elfwork *work)
{
  isize error;
  if (!work)
    return -ERR_INVALID;
  error = unalias(work);
  if (error)
    return error;
  if (!work->memory)
    return 0;
  error = __sys_free(work->memory);
  /* The preparation cleaner can have already removed this exact old Local.
     A reused slot has another generation and is never freed by this value. */
  if (!error || error == -ERR_BAD_HANDLE || error == -ERR_STALE) {
    work->memory = 0;
    return 0;
  }
  return error;
}
static isize
resources(Elfwork *work, Handle handles[3])
{
  Queryrec records[4];
  u64      cursor = 0, written, next, i, kind, slot;
  isize    error;
  handles[0] = handles[1] = handles[2] = 0;
  do {
    error = __sys_query(work->view, QUERY_RESOURCE, cursor, records, ARRAY_SIZE(records), &written,
                        &next);
    if (error)
      return error;
    for (i = 0; i < written; i++) {
      if (records[i].type != QUERY_RESOURCE || records[i].pid != work->pid ||
          records[i].image != work->image)
        return -ERR_STATE;
      kind = records[i].data[1];
      if (kind < 1 || kind > 3)
        continue;
      slot = kind == 2 ? 0 : kind == 3 ? 1 : 2;
      if (handles[slot])
        return -ERR_STATE;
      handles[slot] = (Handle) records[i].data[0];
    }
    cursor = next;
  } while (cursor);
  return handles[0] && handles[1] ? 0 : -ERR_STATE;
}
static isize
startup(Elfwork *work, const void *data, u64 bytes)
{
  Handle    self[3], handle;
  Boot     *boot;
  Resentry *entries;
  Bootview  checked;
  u8       *destination = (u8 *) work->scratch;
  const u8 *source = data;
  u64       i, seen = 0, slot;
  isize     error;
  error = resources(work, self);
  if (error)
    return error;
  error = allocate(work, ALIGN_UP(bytes, PAGE_SIZE), ALLOC_DATA);
  if (!error)
    error = alias(work, ALIGN_UP(bytes, PAGE_SIZE));
  if (error)
    return error;
  for (i = 0; i < bytes; i++)
    destination[i] = source[i];
  boot = (Boot *) destination;
  if (boot->bytes != bytes || boot->resource_count > BOOT_RESOURCE_COUNT ||
      (boot->resources_off & 7) || boot->resources_off < sizeof(Boot) ||
      boot->resources_off > bytes ||
      boot->resource_count * sizeof(Resentry) > bytes - boot->resources_off)
    return -ERR_INVALID;
  entries = (Resentry *) (destination + boot->resources_off);
  for (i = 0; i < boot->resource_count; i++) {
    if (entries[i].tag < BOOT_SELF_SPACE || entries[i].tag > BOOT_CREATE)
      continue;
    slot = entries[i].tag - BOOT_SELF_SPACE;
    handle = self[slot];
    if (!handle || (entries[i].handle && entries[i].handle != handle))
      return -ERR_INVALID;
    entries[i].handle = handle;
    seen |= (u64) 1 << slot;
  }
  if ((seen & 3) != 3)
    return -ERR_INVALID;
  error = bootcheck(destination, bytes, &checked);
  if (!error)
    error = unalias(work);
  if (!error)
    error = mapping(work, work->view, ELF_BOOT_ADDRESS, ALIGN_UP(bytes, PAGE_SIZE), MAP_READ);
  return error ? error : elfclean(work);
}
static isize
page(const Elfsource *source, const Elfplan *plan, Elfwork *work, u64 address)
{
  const Elfseg *segment;
  u64           i, prot = 0, begin, end, file_bytes = 0;
  isize         error;
  for (i = 0; i < plan->count; i++) {
    segment = &plan->segments[i];
    if (address < segment->va + segment->memsz && segment->va < address + PAGE_SIZE)
      prot |= segment->prot;
    begin = MAX(address, segment->va);
    end = MIN(address + PAGE_SIZE, segment->va + segment->filesz);
    if (begin < end)
      file_bytes += end - begin;
  }
  error = allocate(work, PAGE_SIZE, prot & MAP_EXEC ? ALLOC_CODE : ALLOC_DATA);
  if (!error && file_bytes)
    error = alias(work, PAGE_SIZE);
  if (error)
    return error;
  if (file_bytes)
    for (i = 0; i < plan->count; i++) {
      segment = &plan->segments[i];
      begin = MAX(address, segment->va);
      end = MIN(address + PAGE_SIZE, segment->va + segment->filesz);
      if (begin >= end)
        continue;
      error = elfread(source, segment->offset + begin - segment->va,
                      (u8 *) work->scratch + begin - address, end - begin);
      if (error)
        return error;
    }
  error = unalias(work);
  if (!error && (prot & MAP_EXEC))
    error = __sys_seal(work->memory);
  if (!error)
    error = mapping(work, work->view, address, PAGE_SIZE, prot);
  return error ? error : elfclean(work);
}
isize
elfload(const Elfsource *source, const Elfplan *plan, const void *boot, u64 bootbytes,
        Elfwork *work, Config *out)
{
  u64   i, address, end, next = 0;
  isize error, cleanup;
  if (!out)
    return -ERR_INVALID;
  out->entry = out->stack = out->boot = out->bootsz = out->sigmask = out->sigdefault = 0;
  if (!source || !source->read || !plan || !work || !boot || !plan->entry || !plan->pages ||
      !plan->count || plan->count > ELF_PHNUM || plan->reserved || work->reserved || work->mapped ||
      work->memory || work->view <= 0 || work->pid <= 0 || !work->image ||
      work->parent_space <= 0 || (work->scratch & PAGE_MASK) || work->scratch < BOOT_USER_LO ||
      work->scratch >= BOOT_USER_HI || work->scratch_bytes > BOOT_USER_HI - work->scratch ||
      bootbytes < sizeof(Boot) || bootbytes > BOOT_MAX_BYTES ||
      work->scratch_bytes < ALIGN_UP(bootbytes, PAGE_SIZE))
    return -ERR_INVALID;
  if (work->sigmask & ~SIGNAL_SUPPORTED)
    return -ERR_INVALID;
  work->failure = 0;
  error = startup(work, boot, bootbytes);
  for (i = 0; !error && i < plan->count; i++) {
    address = MAX(next, ALIGN_DOWN(plan->segments[i].va, PAGE_SIZE));
    end = ALIGN_UP(plan->segments[i].va + plan->segments[i].memsz, PAGE_SIZE);
    for (; !error && address < end; address += PAGE_SIZE)
      error = page(source, plan, work, address);
    next = MAX(next, end);
  }
  if (!error)
    error = allocate(work, ELF_STACK_BYTES, ALLOC_DATA);
  if (!error)
    error = mapping(work, work->view, ELF_STACK_TOP - ELF_STACK_BYTES, ELF_STACK_BYTES,
                    MAP_READ | MAP_WRITE);
  if (!error)
    error = elfclean(work);
  if (error) {
    work->failure = error;
    cleanup = elfclean(work);
    return cleanup ? cleanup : error;
  }
  out->entry = plan->entry;
  out->stack = ELF_STACK_TOP;
  out->boot = ELF_BOOT_ADDRESS;
  out->bootsz = bootbytes;
  out->sigmask = work->sigmask;
  return 0;
}
