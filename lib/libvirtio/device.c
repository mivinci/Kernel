#include "private/device.h"
#include "private/queue.h"
#define DEVICE_MAGIC       0x74726976U
#define REG_MAGIC          0x00
#define REG_VERSION        0x04
#define REG_DEVICE         0x08
#define REG_VENDOR         0x0c
#define REG_FEATURES       0x10
#define REG_FEATURESEL     0x14
#define REG_DRIVERFEATURES 0x20
#define REG_DRIVERSEL      0x24
#define REG_QUEUESEL       0x30
#define REG_QUEUEMAX       0x34
#define REG_QUEUENUM       0x38
#define REG_QUEUEREADY     0x44
#define REG_QUEUENOTIFY    0x50
#define REG_INTERRUPT      0x60
#define REG_INTERRUPT_ACK  0x64
#define REG_STATUS         0x70
#define REG_QUEUEDESC      0x80
#define REG_QUEUEAVAIL     0x90
#define REG_QUEUEUSED      0xa0
#define STATUS_ACK         1
#define STATUS_DRIVER      2
#define STATUS_DRIVER_OK   4
#define STATUS_FEATURES    8
#define STATUS_NEEDS_RESET 64
#define STATUS_FAILED      128
static int
readreg(Vdev *d, u32 offset, u32 *out)
{
  int error;
  *out = 0;
  error = d->ops.read(d->ops.context, offset, out);
  return error > 0 ? -ERR_STATE : error;
}
static int
writereg(Vdev *d, u32 offset, u32 value)
{
  int error = d->ops.write(d->ops.context, offset, value);
  return error > 0 ? -ERR_STATE : error;
}
static int
fail(Vdev *d, int error)
{
  d->state = VDEV_FAILED;
  return error;
}
static int
status(Vdev *d, u32 value)
{
  int error = writereg(d, REG_STATUS, value);
  if (error)
    return fail(d, error);
  d->status = value;
  return 0;
}
static int
reject(Vdev *d, int error)
{
  /* A transport error can prevent FAILED from reaching the device. In either
     case only a later successful reset may return to initialization. */
  status(d, d->status | STATUS_FAILED);
  return fail(d, error);
}
static int
checkstatus(Vdev *d)
{
  u32 value, expected = d->status;
  u32 allowed = STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES | STATUS_FAILED | STATUS_NEEDS_RESET;
  int error = readreg(d, REG_STATUS, &value);
  if (error)
    return fail(d, error);
  d->status = value;
  if (value & ~(allowed | (expected & STATUS_DRIVER_OK)))
    return fail(d, -ERR_PEER);
  if (value != expected || (value & (STATUS_FAILED | STATUS_NEEDS_RESET)))
    return reject(d, -ERR_PEER);
  return 0;
}
static int
addstatus(Vdev *d, u32 bits)
{
  int error = checkstatus(d);
  return error ? error : status(d, d->status | bits);
}
int
vprobe(Vdev *d, const Vdevops *ops)
{
  u32 value;
  int error;
  if (!d || !ops || !ops->read || !ops->write)
    return -ERR_INVALID;
  d->offered = d->features = d->deadline = d->lasttime = 0;
  d->device = d->vendor = d->status = d->attempts = d->queues = 0;
  d->state = VDEV_FAILED;
  d->ops.context = ops->context;
  d->ops.read = ops->read;
  d->ops.write = ops->write;
  error = readreg(d, REG_MAGIC, &value);
  if (error || value != DEVICE_MAGIC)
    return error ? error : -ERR_INVALID;
  error = readreg(d, REG_VERSION, &value);
  if (error || value != 2)
    return error ? error : -ERR_NOTSUP;
  error = readreg(d, REG_DEVICE, &value);
  if (error)
    return error;
  d->device = value;
  if (!value) {
    d->state = VDEV_EMPTY;
    return 0;
  }
  error = readreg(d, REG_VENDOR, &value);
  if (error)
    return error;
  d->vendor = value;
  d->state = VDEV_FOUND;
  return 0;
}
int
vresetpoll(Vdev *d, u64 now)
{
  u32 value;
  int error;
  if (!d || d->state != VDEV_RESETTING)
    return -ERR_STATE;
  if (now < d->lasttime)
    return -ERR_INVALID;
  d->lasttime = now;
  if (now >= d->deadline)
    return fail(d, -ERR_TIMEDOUT);
  error = readreg(d, REG_STATUS, &value);
  if (error)
    return fail(d, error);
  d->status = value;
  if (value)
    return -ERR_WOULD_BLOCK;
  d->offered = d->features = 0;
  d->attempts = d->queues = 0;
  d->state = VDEV_RESET;
  return 0;
}
int
vresetbegin(Vdev *d, u64 now)
{
  int error;
  if (!d || !d->ops.read || !d->ops.write || d->state < VDEV_FOUND || d->state > VDEV_LIVE ||
      d->state == VDEV_RESETTING || !d->device)
    return -ERR_STATE;
  if (d->attempts >= VDEV_RESET_TRIES)
    return -ERR_LIMIT;
  if (now > ~(u64) 0 - VDEV_RESET_NS || now < d->lasttime)
    return -ERR_INVALID;
  d->deadline = now + VDEV_RESET_NS;
  d->lasttime = now;
  d->attempts++;
  error = status(d, 0);
  if (error)
    return error;
  d->state = VDEV_RESETTING;
  return vresetpoll(d, now);
}
int
vfeatures(Vdev *d, u64 wanted, u64 required)
{
  u64 offered, allowed;
  u32 low, high, value;
  int error;
  if (!d || d->state != VDEV_RESET)
    return -ERR_STATE;
  wanted |= VDEV_VERSION1;
  required |= VDEV_VERSION1;
  if (required & ~wanted)
    return -ERR_INVALID;
  allowed = VDEV_VERSION1 | 0xffffffU;
  if (wanted & ~allowed)
    return -ERR_NOTSUP;
  error = addstatus(d, STATUS_ACK);
  if (!error)
    error = addstatus(d, STATUS_DRIVER);
  if (error)
    return error;
  error = writereg(d, REG_FEATURESEL, 0);
  if (!error)
    error = readreg(d, REG_FEATURES, &low);
  if (!error)
    error = writereg(d, REG_FEATURESEL, 1);
  if (!error)
    error = readreg(d, REG_FEATURES, &high);
  if (error)
    return fail(d, error);
  offered = (u64) high << 32 | low;
  d->offered = offered;
  if ((offered & required) != required)
    return reject(d, -ERR_NOTSUP);
  wanted &= offered;
  error = writereg(d, REG_DRIVERSEL, 0);
  if (!error)
    error = writereg(d, REG_DRIVERFEATURES, (u32) wanted);
  if (!error)
    error = writereg(d, REG_DRIVERSEL, 1);
  if (!error)
    error = writereg(d, REG_DRIVERFEATURES, (u32) (wanted >> 32));
  if (!error)
    error = addstatus(d, STATUS_FEATURES);
  if (!error)
    error = readreg(d, REG_STATUS, &value);
  if (error)
    return fail(d, error);
  d->status = value;
  /* DRIVER_OK and reserved bits were never written by this initializer. Do
     not echo an unexpected status and accidentally enable an alien session. */
  if (value & ~(STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES | STATUS_FAILED | STATUS_NEEDS_RESET))
    return fail(d, -ERR_PEER);
  if (value != (STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES))
    return reject(d, -ERR_PEER);
  d->features = wanted;
  d->state = VDEV_FEATURES;
  return 0;
}
static int
writeaddress(Vdev *d, u32 reg, u64 address)
{
  int error = writereg(d, reg, (u32) address);
  return error ? error : writereg(d, reg + 4, (u32) (address >> 32));
}
int
vqueue(Vdev *d, u32 index, u64 desc, u64 avail, u64 used)
{
  u32 value, bit;
  int error;
  if (!d || d->state != VDEV_FEATURES)
    return -ERR_STATE;
  if (index >= VDEV_QUEUES || ((desc | avail | used) & (PAGE_SIZE - 1)) || desc == avail ||
      desc == used || avail == used)
    return -ERR_INVALID;
  bit = 1U << index;
  if (d->queues & bit)
    return -ERR_BUSY;
  error = checkstatus(d);
  if (error)
    return error;
  error = writereg(d, REG_QUEUESEL, index);
  if (!error)
    error = readreg(d, REG_QUEUEREADY, &value);
  if (error)
    return fail(d, error);
  if (value)
    return reject(d, -ERR_PEER);
  error = readreg(d, REG_QUEUEMAX, &value);
  if (error)
    return fail(d, error);
  if (value < VQ_SIZE)
    return -ERR_NOTSUP;
  error = writereg(d, REG_QUEUENUM, VQ_SIZE);
  if (!error)
    error = writeaddress(d, REG_QUEUEDESC, desc);
  if (!error)
    error = writeaddress(d, REG_QUEUEAVAIL, avail);
  if (!error)
    error = writeaddress(d, REG_QUEUEUSED, used);
  if (!error)
    error = writereg(d, REG_QUEUEREADY, 1);
  if (!error)
    error = readreg(d, REG_QUEUEREADY, &value);
  if (error)
    return fail(d, error);
  if (value != 1)
    return reject(d, -ERR_PEER);
  d->queues |= bit;
  return 0;
}
int
vstart(Vdev *d)
{
  int error;
  if (!d || d->state != VDEV_FEATURES || !d->queues)
    return -ERR_STATE;
  error = addstatus(d, STATUS_DRIVER_OK);
  if (!error)
    error = checkstatus(d);
  if (error)
    return error;
  d->state = VDEV_LIVE;
  return 0;
}
int
vnotify(Vdev *d, u32 index)
{
  int error;
  if (!d || d->state != VDEV_LIVE)
    return -ERR_STATE;
  if (index >= VDEV_QUEUES || !(d->queues & (1U << index)))
    return -ERR_INVALID;
  error = writereg(d, REG_QUEUENOTIFY, index);
  return error ? fail(d, error) : 0;
}
int
vinterrupt(Vdev *d, u32 *causes)
{
  u32 pending;
  int error;
  if (!causes)
    return -ERR_INVALID;
  if (!d || d->state != VDEV_LIVE)
    return -ERR_STATE;
  error = checkstatus(d);
  if (error)
    return error;
  error = readreg(d, REG_INTERRUPT, &pending);
  if (error)
    return fail(d, error);
  if (pending & ~(VDEV_IRQ_USED | VDEV_IRQ_CONFIG))
    return fail(d, -ERR_PEER);
  if (pending) {
    error = writereg(d, REG_INTERRUPT_ACK, pending);
    if (error)
      return fail(d, error);
  }
  *causes = pending;
  return 0;
}
