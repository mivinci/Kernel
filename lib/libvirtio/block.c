#include "private/block.h"

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
