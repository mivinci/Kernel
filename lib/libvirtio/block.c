#include "private/block.h"

#define REG_CONFIG_GENERATION 0xfc
#define REG_CAPACITY_LO       0x100
#define REG_CAPACITY_HI       0x104

int
vblkfeatures(Vdev *device)
{
  if (!device || device->device != VBLK_DEVICE)
    return -ERR_INVALID;
  return vfeatures(device, VBLK_F_RO | VBLK_F_FLUSH, 0);
}

int
vblkconfig(const Vdev *device, Vblkconfig *out)
{
  u32 before = 0, low = 0, high = 0, after = 0;
  int error;
  if (!device || !out || device->device != VBLK_DEVICE || !device->ops.read)
    return -ERR_INVALID;
  if (device->state != VDEV_FEATURES ||
      ((device->offered & VBLK_F_RO) && !(device->features & VBLK_F_RO)))
    return -ERR_STATE;
  error = device->ops.read(device->ops.context, REG_CONFIG_GENERATION, &before);
  if (!error)
    error = device->ops.read(device->ops.context, REG_CAPACITY_LO, &low);
  if (!error)
    error = device->ops.read(device->ops.context, REG_CAPACITY_HI, &high);
  if (!error)
    error = device->ops.read(device->ops.context, REG_CONFIG_GENERATION, &after);
  if (error)
    return error < 0 ? error : -ERR_STATE;
  if (before != after)
    return -ERR_WOULD_BLOCK;
  out->sectors = (u64) high << 32 | low;
  out->flush = !!(device->features & VBLK_F_FLUSH);
  out->readonly = !out->flush || !!(device->features & VBLK_F_RO);
  return 0;
}

static int
extent(u64 base, u64 bytes)
{
  return bytes <= UINT64_MAX - base;
}

static int
overlap(u64 a, u64 an, u64 b, u64 bn)
{
  return a < b + bn && b < a + an;
}

int
vblkprepare(u32 type, u64 sector, u64 capacity, u64 header, u64 data, u32 bytes, u64 status,
            Vblkhead *wire, Vbuf buffers[3])
{
  u32 tail = 1;
  if (!wire || !buffers || !extent(header, sizeof(*wire)) || !extent(status, 1) ||
      overlap(header, sizeof(*wire), status, 1))
    return -ERR_INVALID;
  if (type == VBLK_FLUSH) {
    if (sector || data || bytes)
      return -ERR_INVALID;
  } else if (type == VBLK_READ || type == VBLK_WRITE) {
    if (!bytes || bytes % VBLK_SECTOR || sector >= capacity ||
        bytes / VBLK_SECTOR > capacity - sector || !extent(data, bytes) ||
        overlap(data, bytes, header, sizeof(*wire)) || overlap(data, bytes, status, 1))
      return -ERR_INVALID;
    tail = 2;
  } else
    return -ERR_NOTSUP;
  wire->type = type;
  wire->reserved = 0;
  wire->sector = sector;
  buffers[0].address = header;
  buffers[0].length = sizeof(*wire);
  buffers[0].writable = 0;
  if (tail == 2) {
    buffers[1].address = data;
    buffers[1].length = bytes;
    buffers[1].writable = type == VBLK_READ;
  }
  buffers[tail].address = status;
  buffers[tail].length = 1;
  buffers[tail].writable = 1;
  return tail + 1;
}

int
vblkresult(u32 type, u32 bytes, u32 written, u8 status)
{
  u32 limit = 1;
  if (type == VBLK_READ || type == VBLK_WRITE) {
    if (!bytes || bytes % VBLK_SECTOR)
      return -ERR_INVALID;
    if (type == VBLK_READ)
      limit += bytes;
  } else if (type != VBLK_FLUSH || bytes)
    return -ERR_INVALID;
  if (!written || written > limit || status > VBLK_UNSUPPORTED ||
      (status == VBLK_OK && written != limit))
    return -ERR_PEER;
  return status;
}
