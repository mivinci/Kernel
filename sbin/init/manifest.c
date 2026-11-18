#include "manifest.h"
#include <abi/bootpkg.h>

static u64
read64(const u8 *data)
{
  u64 value = 0;
  u32 i;
  for (i = 0; i < 8; i++)
    value |= (u64) data[i] << (i * 8);
  return value;
}

isize
manifestopen(const void *input, u64 bytes, u64 machine, Manifest *out)
{
  const u8 *data = input, *row;
  Manifest  checked;
  u64       count, hash = UINT64_C(0xcbf29ce484222325), mask = 0, done = 0, before, i, j, k;
  if (!input || !out || bytes < 64 || (machine != BOOTPKG_ARM64 && machine != BOOTPKG_RISCV))
    return -ERR_INVALID;
  count = read64(data + 32);
  if (read64(data) != INITCFG_MAGIC || read64(data + 8) != INITCFG_FORMAT ||
      read64(data + 16) != bytes || read64(data + 24) != machine || !count ||
      count > INITCFG_SERVICES || read64(data + 40) != 64 || read64(data + 48) != INITCFG_RECORD ||
      bytes != 64 + count * INITCFG_RECORD)
    return -ERR_INVALID;
  for (i = 0; i < bytes; i++) {
    hash ^= i >= 56 && i < 64 ? 0 : data[i];
    hash *= UINT64_C(0x100000001b3);
  }
  if (hash != read64(data + 56))
    return -ERR_INVALID;
  for (i = 0; i < sizeof(checked); i++)
    ((u8 *) &checked)[i] = 0;
  checked.count = count;
  for (i = 0; i < count; i++) {
    Servicecfg *service = &checked.service[i];
    row = data + 64 + i * INITCFG_RECORD;
    service->number = read64(row);
    service->kind = read64(row + 8);
    service->flags = read64(row + 16);
    service->creator = read64(row + 24);
    service->memlimit = read64(row + 32);
    service->exec_limit = read64(row + 40);
    service->resources = read64(row + 48);
    service->pending = read64(row + 56);
    service->dependencies = read64(row + 64);
    for (j = 0; j < INITCFG_NAME_BYTES; j++)
      service->image[j] = row[72 + j];
    for (j = 0; j < 3; j++)
      service->reserved[j] = read64(row + 136 + j * 8);
    if (!service->number || service->number > INITCFG_SERVICES ||
        mask & (UINT64_C(1) << (service->number - 1)) || service->kind != INITCFG_SERIAL ||
        service->flags != INITCFG_REQUIRED || service->creator != BOOT_CREATE ||
        !service->memlimit || service->memlimit > INITCFG_MAX_PAGES ||
        service->exec_limit < service->memlimit || service->exec_limit > INITCFG_MAX_EXEC ||
        service->resources < 9 || service->resources > BOOT_RESOURCE_COUNT || !service->pending ||
        service->pending > INITCFG_PENDING ||
        service->dependencies & ~((UINT64_C(1) << INITCFG_SERVICES) - 1) || service->reserved[0] ||
        service->reserved[1] || service->reserved[2])
      return -ERR_INVALID;
    for (j = 0; j < INITCFG_NAME_BYTES && service->image[j]; j++) {
    }
    if (!j || j == INITCFG_NAME_BYTES)
      return -ERR_INVALID;
    for (k = j; k < INITCFG_NAME_BYTES; k++)
      if (service->image[k])
        return -ERR_INVALID;
    mask |= UINT64_C(1) << (service->number - 1);
  }
  for (i = 0; i < count; i++)
    if (checked.service[i].dependencies & ~mask)
      return -ERR_INVALID;
  for (k = 0; k < count; k++) {
    before = done;
    for (i = 0; i < count; i++)
      if (!(checked.service[i].dependencies & ~done))
        done |= UINT64_C(1) << (checked.service[i].number - 1);
    if (done == mask)
      break;
    if (done == before)
      return -ERR_INVALID;
  }
  /* Only the selected console UART exists in the first inventory contract. */
  if (count != 1)
    return -ERR_NOTSUP;
  *out = checked;
  return 0;
}
