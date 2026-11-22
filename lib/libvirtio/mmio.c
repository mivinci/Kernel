#include "private/mmio.h"

static int
within(const Vmmio *mmio, u32 offset, u32 width)
{
  return mmio && offset <= mmio->bytes && width <= mmio->bytes - offset && !(offset & (width - 1));
}

int
virtioread(void *context, u32 offset, u32 *out)
{
  const Vmmio *mmio = context;
  if (!out || !within(mmio, offset, 4))
    return -ERR_INVALID;
  virtiofence(NULL, 0);
  *out = *(volatile u32 *) (mmio->base + offset);
  virtiofence(NULL, 0);
  return 0;
}

static int
readbyte(void *context, u32 offset, u8 *out)
{
  const Vmmio *mmio = context;
  if (!out || offset < 0x100 || !within(mmio, offset, 1))
    return -ERR_INVALID;
  virtiofence(NULL, 0);
  *out = *(volatile u8 *) (mmio->base + offset);
  virtiofence(NULL, 0);
  return 0;
}

static int
write(void *context, u32 offset, u32 value)
{
  const Vmmio *mmio = context;
  if (offset >= 0x100 || !within(mmio, offset, 4))
    return -ERR_INVALID;
  virtiofence(NULL, 0);
  *(volatile u32 *) (mmio->base + offset) = value;
  virtiofence(NULL, 0);
  return 0;
}

int
vmmioinit(Vmmio *mmio, Vdevops *ops, uintptr_t base, u64 bytes)
{
  if (!mmio || !ops || !base || (base & 3) || bytes < 0x100 || bytes > UINT64_MAX - base)
    return -ERR_INVALID;
  *mmio = (Vmmio){base, bytes};
  *ops = (Vdevops){mmio, virtioread, write, readbyte};
  return 0;
}
