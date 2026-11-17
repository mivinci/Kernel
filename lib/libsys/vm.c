#include "private/vm.h"
#include "private/syscall.h"
#include "private/start.h"
#define VM_ACTIVE   1
#define VM_CLEANUP  2
#define VM_BUILDING 3
typedef struct Vregion Vregion;
struct Vregion
{
  u64    address, bytes;
  Handle handle;
  u32    mapped, state;
};
static Vregion regions[SYSVM_REGIONS];
static u64     low, high, cursor;
static u32     initialized;
int
sysvmready(void)
{
  return initialized != 0;
}
isize
sysvminit(u64 first, u64 end)
{
  u32 i;
  if (!first || first >= end || ((first | end) & PAGE_MASK))
    return -ERR_INVALID;
  for (i = 0; i < SYSVM_REGIONS; i++)
    if (regions[i].state)
      return first == low && end == high ? 0 : -ERR_STATE;
  low = cursor = first;
  high = end;
  initialized = 1;
  return 0;
}
static isize
cleanup(Vregion *r, Handle space)
{
  isize error;
  if (r->handle) {
    error = __sys_free(r->handle);
    if (error && error != -ERR_BAD_HANDLE && error != -ERR_STALE && error != -ERR_REVOKED)
      return error;
    r->handle = 0;
  }
  while (r->mapped) {
    error = __sys_unmap(space, r->address + (u64) (r->mapped - 1) * SYSVM_CHUNK);
    if (error)
      return error;
    r->mapped--;
  }
  r->address = r->bytes = 0;
  r->state = 0;
  return 0;
}
static int
findrange(u64 bytes, u64 *out)
{
  u64 candidate, limit;
  u32 pass, step, i;
  for (pass = 0; pass < 2; pass++) {
    candidate = pass ? low : cursor;
    limit = high;
    for (step = 0; step <= SYSVM_REGIONS; step++) {
      if (candidate > limit || bytes > limit - candidate)
        break;
      for (i = 0; i < SYSVM_REGIONS; i++)
        if (regions[i].state && candidate < regions[i].address + regions[i].bytes &&
            regions[i].address < candidate + bytes)
          break;
      if (i == SYSVM_REGIONS) {
        *out = candidate;
        return 1;
      }
      candidate = regions[i].address + regions[i].bytes;
    }
  }
  return 0;
}
isize
sysvmget(u64 bytes, void **out)
{
  Allocarg allocation;
  Maparg   mapping;
  Handle   space, authority;
  Vregion *r;
  u64      address, offset, amount;
  u32      i, attempt;
  isize    error, released;
  if (!out)
    return -ERR_INVALID;
  *out = NULL;
  if (!initialized || !bytes || (bytes & PAGE_MASK) || bytes > high - low)
    return -ERR_INVALID;
  if ((bytes - 1) / SYSVM_CHUNK >= UINT32_MAX)
    return -ERR_OVERFLOW;
  space = bootresource(sysboot(), BOOT_SELF_SPACE);
  authority = bootresource(sysboot(), BOOT_SELF_ALLOC);
  if (space <= 0 || authority <= 0)
    return -ERR_BAD_HANDLE;
  for (i = 0; i < SYSVM_REGIONS; i++)
    if (regions[i].state == VM_CLEANUP)
      cleanup(&regions[i], space);
  for (i = 0; i < SYSVM_REGIONS; i++)
    if (!regions[i].state)
      break;
  if (i == SYSVM_REGIONS)
    return -ERR_CAPACITY;
  r = &regions[i];
  allocation.kind = ALLOC_DATA;
  allocation.align = allocation.minaddr = allocation.maxaddr = allocation.reserved = 0;
  mapping.offset = 0;
  mapping.prot = MAP_READ | MAP_WRITE;
  mapping.reserved = 0;
  for (attempt = 0; attempt < SYSVM_PROBES; attempt++) {
    if (!findrange(bytes, &address))
      return -ERR_NOMEM;
    r->address = address;
    r->bytes = bytes;
    r->handle = 0;
    r->mapped = 0;
    r->state = VM_BUILDING;
    cursor = address + bytes;
    if (cursor == high)
      cursor = low;
    error = 0;
    for (offset = 0; offset < bytes; offset += amount) {
      amount = MIN(bytes - offset, SYSVM_CHUNK);
      allocation.bytes = amount;
      error = __sys_alloc(authority, &allocation, &r->handle);
      if (error)
        break;
      mapping.mem = r->handle;
      mapping.address = address + offset;
      mapping.length = amount;
      error = __sys_map(space, &mapping);
      if (error)
        break;
      r->mapped++;
      error = __sys_free(r->handle);
      if (error)
        break;
      r->handle = 0;
    }
    if (!error) {
      r->state = VM_ACTIVE;
      *out = (void *) address;
      return 0;
    }
    r->state = VM_CLEANUP;
    released = cleanup(r, space);
    if (released)
      return released;
    if (error != -ERR_STATE)
      return error;
  }
  return -ERR_NOMEM;
}
isize
sysvmput(void *address)
{
  Handle space;
  u32    i;
  if (!initialized || !address)
    return -ERR_INVALID;
  space = bootresource(sysboot(), BOOT_SELF_SPACE);
  if (space <= 0)
    return -ERR_BAD_HANDLE;
  for (i = 0; i < SYSVM_REGIONS; i++)
    if (regions[i].state == VM_ACTIVE && regions[i].address == (u64) address)
      break;
  if (i == SYSVM_REGIONS)
    return -ERR_BAD_HANDLE;
  regions[i].state = VM_CLEANUP;
  return cleanup(&regions[i], space);
}
