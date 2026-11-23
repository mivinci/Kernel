#include "private/elf.h"
#include "private/fix.h"
#include <abi/resource.h>
isize
sysfixchild(const Forkret *result, u64 available, Handle source, u64 source_image, Fixview *out)
{
  Handle self, parent;
  u64    image, address;
  isize  error;
  if (!out)
    return -ERR_INVALID;
  out->header = NULL;
  out->resources = NULL;
  out->contexts = NULL;
  out->contexts_limit = 0;
  if (!result || result->construct || available < sizeof(Fix) || available > BOOT_MAX_BYTES)
    return -ERR_INVALID;
  address = (u64) result->fix;
  if (address < BOOT_USER_LO || address >= BOOT_USER_HI || available > BOOT_USER_HI - address ||
      (address & 7))
    return -ERR_INVALID;
  error = __sys_getid(&self, &image, &parent);
  if (error)
    return error;
  if (self != result->pid)
    return -ERR_STALE;
  /* The current parent may have changed since the snapshot. */
  return sysfixview(result->fix, available, source, source_image, self, out);
}
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
isize
sysfixload(Elfwork *work, u64 address, const void *data, u64 bytes)
{
  Fixview view;
  Handle  source, parent;
  u64     image, length, input = (u64) data;
  isize   error, cleanup;
  if (!work || work->reserved || work->mapped || work->memory || work->view <= 0 ||
      work->parent_space <= 0 || bytes < sizeof(Fix) || bytes > BOOT_MAX_BYTES ||
      input < BOOT_USER_LO || input >= BOOT_USER_HI || bytes > BOOT_USER_HI - input)
    return -ERR_INVALID;
  length = ALIGN_UP(bytes, PAGE_SIZE);
  if ((work->scratch | address) & PAGE_MASK || work->scratch < BOOT_USER_LO ||
      work->scratch >= BOOT_USER_HI || length > BOOT_USER_HI - work->scratch ||
      work->scratch_bytes < length || address < BOOT_USER_LO || address >= BOOT_USER_HI ||
      length > BOOT_USER_HI - address ||
      (input < work->scratch + length && work->scratch < input + bytes))
    return -ERR_INVALID;
  work->failure = 0;
  error = __sys_getid(&source, &image, &parent);
  if (!error)
    error = allocate(work, length, ALLOC_DATA);
  if (!error)
    error = alias(work, length);
  if (!error)
    error = __sys_copy((void *) work->scratch, data, bytes);
  if (!error)
    error = sysfixview((const void *) work->scratch, bytes, source, image, work->pid, &view);
  if (!error && view.header->bytes != bytes)
    error = -ERR_INVALID;
  if (!error)
    error = unalias(work);
  if (!error)
    error = mapping(work, work->view, address, length, MAP_READ);
  work->failure = error;
  cleanup = elfclean(work);
  return cleanup ? cleanup : error;
}
static isize
resourcepage(Elfwork *work, u64 cursor, Queryrec *records, u64 count, u64 *written, u64 *next)
{
  u64   i;
  isize error = __sys_query(work->view, QUERY_RESOURCE, cursor, records, count, written, next);
  if (error)
    return error;
  if (*written > count || (*next && (!*written || *next == cursor)))
    return -ERR_STATE;
  for (i = 0; i < *written; i++)
    if (records[i].type != QUERY_RESOURCE || records[i].pid != work->pid ||
        records[i].image != work->image)
      return -ERR_STATE;
  return 0;
}
static isize
resources(Elfwork *work, Handle handles[3])
{
  Queryrec records[4];
  u64      cursor = 0, written, next, i, kind, slot, total = 0;
  isize    error;
  handles[0] = handles[1] = handles[2] = 0;
  do {
    error = resourcepage(work, cursor, records, ARRAY_SIZE(records), &written, &next);
    if (error)
      return error;
    total += written;
    if (total > BOOT_RESOURCE_COUNT)
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      kind = records[i].data[1];
      if (kind < RESOURCE_CREATE || kind > RESOURCE_ALLOC)
        continue;
      slot = kind == RESOURCE_SPACE ? 0 : kind == RESOURCE_ALLOC ? 1 : 2;
      if (handles[slot])
        return -ERR_STATE;
      handles[slot] = (Handle) records[i].data[0];
    }
    cursor = next;
  } while (cursor);
  return handles[0] && handles[1] ? 0 : -ERR_STATE;
}
static isize
fdcalls(Elfwork *work, const Bootview *boot)
{
  Queryrec records[4];
  Handle   needed[BOOT_RESOURCE_COUNT];
  u64      left = 0, cursor = 0, written, next, i, total = 0;
  isize    error;
  if (!boot->header->fd_count)
    return 0;
  for (i = 0; i < ARRAY_SIZE(needed); i++)
    ((volatile Handle *) needed)[i] = 0;
  for (i = 0; i < boot->header->fd_count; i++) {
    Handle call = boot->fds[i].backend_call;
    u32    slot = (u64) call & UINT16_MAX;
    if (slot >= ARRAY_SIZE(needed) || (needed[slot] && needed[slot] != call))
      return -ERR_BAD_HANDLE;
    if (!needed[slot]) {
      needed[slot] = call;
      left++;
    }
  }
  do {
    error = resourcepage(work, cursor, records, ARRAY_SIZE(records), &written, &next);
    if (error)
      return error;
    total += written;
    if (total > BOOT_RESOURCE_COUNT)
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      const Queryrec *record = &records[i];
      u32             slot = record->data[0] & UINT16_MAX;
      if (slot >= ARRAY_SIZE(needed) || !needed[slot] || needed[slot] != (Handle) record->data[0])
        continue;
      if (record->data[1] != RESOURCE_CALL)
        return -ERR_BAD_HANDLE;
      if (!(record->data[2] & RESOURCE_USE))
        return -ERR_DENIED;
      if (!pidvalid((Handle) record->data[6]) || !record->data[7])
        return -ERR_STATE;
      needed[slot] = 0;
      left--;
    }
    cursor = next;
  } while (left && cursor);
  return left ? -ERR_BAD_HANDLE : 0;
}
static isize
inheritedcreate(Handle handle)
{
  Queryrec rows[4];
  u64 cursor = 0, count, next, i;
  isize error;
  do {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &count, &next);
    if (error)
      return error;
    for (i = 0; i < count; i++)
      if ((Handle) rows[i].data[0] == handle && rows[i].data[1] == RESOURCE_CREATE &&
          (rows[i].data[2] & RESOURCE_USE))
        return 0;
    cursor = next;
  } while (cursor);
  return -ERR_BAD_HANDLE;
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
    /* Exec moves CREATE at commit, after preparation-table enumeration. */
    if (!handle && slot == 2 && work->image > 1 && entries[i].handle &&
        !inheritedcreate(entries[i].handle))
      handle = entries[i].handle;
    if (!handle || (entries[i].handle && entries[i].handle != handle))
      return -ERR_INVALID;
    entries[i].handle = handle;
    seen |= (u64) 1 << slot;
  }
  if ((seen & 3) != 3)
    return -ERR_INVALID;
  error = bootcheck(destination, bytes, &checked);
  if (!error)
    error = fdcalls(work, &checked);
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
