#include "private/block.h"

#define REG_CONFIG_GENERATION 0xfc
#define REG_CAPACITY_LO       0x100
#define REG_CAPACITY_HI       0x104
#define REG_BLOCK_SIZE        0x114
#define REG_PHYSICAL_EXP      0x118
#define REG_ALIGNMENT_OFFSET  0x119

static int
validblocksize(u32 bytes)
{
  return bytes >= VBLK_SECTOR && !(bytes & (bytes - 1));
}

int
vblkfeatures(Vdev *device)
{
  u64 wanted = VBLK_F_RO | VBLK_F_BLK_SIZE | VBLK_F_FLUSH;
  if (!device || device->device != VBLK_DEVICE)
    return -ERR_INVALID;
  if (device->ops.readbyte)
    wanted |= VBLK_F_TOPOLOGY;
  return vfeatures(device, wanted, 0);
}

int
vblkconfig(const Vdev *device, Vblkconfig *out)
{
  u32 before = 0, low = 0, high = 0, after = 0, blocksize = VBLK_SECTOR;
  u32 physical = 0, alignment = 0;
  u8  exponent = 0, offset = 0;
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
  if (!error && device->features & VBLK_F_BLK_SIZE)
    error = device->ops.read(device->ops.context, REG_BLOCK_SIZE, &blocksize);
  if (!error && device->features & VBLK_F_TOPOLOGY) {
    if (!device->ops.readbyte)
      return -ERR_STATE;
    error = device->ops.readbyte(device->ops.context, REG_PHYSICAL_EXP, &exponent);
    if (!error)
      error = device->ops.readbyte(device->ops.context, REG_ALIGNMENT_OFFSET, &offset);
  }
  if (!error)
    error = device->ops.read(device->ops.context, REG_CONFIG_GENERATION, &after);
  if (error)
    return error < 0 ? error : -ERR_STATE;
  if (before != after)
    return -ERR_WOULD_BLOCK;
  if (!validblocksize(blocksize))
    return -ERR_STATE;
  if (device->features & VBLK_F_TOPOLOGY) {
    if (exponent >= 32 || blocksize > (UINT32_MAX >> exponent) || offset >= (1U << exponent))
      return -ERR_STATE;
    physical = blocksize << exponent;
    alignment = blocksize * offset;
  }
  out->sectors = (u64) high << 32 | low;
  out->flush = !!(device->features & VBLK_F_FLUSH);
  out->readonly = !out->flush || !!(device->features & VBLK_F_RO);
  out->blocksize = blocksize;
  out->physical = physical;
  out->alignment = alignment;
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

static int
buffersync(Vblkreq *r, u32 phase, u32 index)
{
  int error = r->sync(r->context, phase, &r->buffers[index]);
  return error > 0 ? -ERR_STATE : error;
}
static int restorebuffers(Vblkreq *r);

int
vblksubmit(Vblkreq *r, Vring *ring, const Vblkconfig *config, u32 type, u64 sector, u64 cookie,
           u64 header, u64 data, u32 bytes, u64 status, Vblkhead *wire, volatile u8 *flag,
           Vblksync sync, void *context)
{
  u32 i, published;
  int count, error;
  if (!r || !ring || !config || !flag || !sync || !cookie)
    return -ERR_INVALID;
  if (r->held || r->active)
    return -ERR_BUSY;
  if ((type == VBLK_WRITE && config->readonly) || (type == VBLK_FLUSH && !config->flush))
    return -ERR_NOTSUP;
  if ((type == VBLK_READ || type == VBLK_WRITE) &&
      (!validblocksize(config->blocksize) || bytes % config->blocksize ||
       sector % (config->blocksize / VBLK_SECTOR)))
    return -ERR_INVALID;
  count = vblkprepare(type, sector, config->sectors, header, data, bytes, status, wire, r->buffers);
  if (count < 0)
    return count;
  *flag = VBLK_PENDING;
  r->status = flag;
  r->sync = sync;
  r->context = context;
  r->cookie = cookie;
  r->generation = ring->queue.generation;
  r->type = type;
  r->bytes = bytes;
  for (i = 0; i < (u32) count; i++) {
    /* A failing synchronization can have had a partial effect. */
    r->held |= 1U << i;
    error = buffersync(r, VR_BEFORE, i);
    if (error)
      return error;
  }
  error = vrsubmit(ring, r->buffers, (u32) count, cookie, &published);
  if (!published) {
    int restore = restorebuffers(r);
    return restore ? restore : error;
  }
  r->active = 1;
  return error;
}

static int
restorebuffers(Vblkreq *r)
{
  u32 i;
  int error;
  r->active = 0;
  for (i = 0; i < 3; i++) {
    if (!(r->held & (1U << i)))
      continue;
    error = buffersync(r, VR_AFTER, i);
    if (error)
      return error;
    r->held &= ~(1U << i);
  }
  return 0;
}
int
vblkreclaim(Vblkreq *r)
{
  return r ? restorebuffers(r) : -ERR_INVALID;
}

int
vblkfinish(Vblkreq *r, const Vdone *done)
{
  int error;
  if (!r || !done || !r->active)
    return -ERR_STATE;
  if (done->cookie != r->cookie || done->generation != r->generation)
    return -ERR_STALE;
  error = restorebuffers(r);
  return error ? error : vblkresult(r->type, r->bytes, done->length, *r->status);
}
