/* Internal modern MMIO negotiation. MMIO authority belongs to the caller. */
#ifndef _VIRTIO_DEVICE_H
#define _VIRTIO_DEVICE_H
#include <types.h>
#include <abi/base.h>
#define VDEV_EMPTY       1
#define VDEV_FOUND       2
#define VDEV_RESETTING   3
#define VDEV_RESET       4
#define VDEV_FEATURES    5
#define VDEV_FAILED      6
#define VDEV_LIVE        7
#define VDEV_QUEUES      32
#define VDEV_IRQ_USED    1U
#define VDEV_IRQ_CONFIG  2U
#define VDEV_VERSION1    ((u64) 1 << 32)
#define VDEV_RESET_NS    UINT64_C(3000000000)
#define VDEV_RESET_TRIES 3

typedef struct Vdevops Vdevops;
typedef struct Vdev    Vdev;
struct Vdevops
{
  void *context;
  /* Aligned 32-bit MMIO, with platform device-access ordering/completion.
     Return 0 or negative status. No reentrant access to this device. */
  int (*read)(void *context, u32 offset, u32 *out);
  int (*write)(void *context, u32 offset, u32 value);
  /* Byte access to device-specific configuration fields. Optional. */
  int (*readbyte)(void *context, u32 offset, u8 *out);
};
struct Vdev
{
  u64     offered, features, deadline, lasttime;
  u32     device, vendor, state, status, attempts, queues;
  Vdevops ops;
};
/* Fresh private storage. A valid empty transport succeeds with state EMPTY;
   no register after DeviceID is accessed in that case. */
int vprobe(Vdev *device, const Vdevops *ops);
/* Nonblocking reset: now is monotonic nanoseconds. begin writes zero and polls
   once; poll returns WOULD_BLOCK until actual status zero, or TIMEDOUT at 3s.
   Three consecutive failed attempts prohibit further retries in this object.
   Neither timeout nor write success proves DMA stopped. Successful reset does
   not perform buffer AFTER maintenance or release native DMA pins. */
int vresetbegin(Vdev *device, u64 now);
int vresetpoll(Vdev *device, u64 now);
/* VERSION_1 is mandatory. Only device-specific bits 0..23 and VERSION_1 may
   be selected; unsupported common optimizations are never negotiated.
   Does not configure queues or set DRIVER_OK. */
int vfeatures(Vdev *device, u64 wanted, u64 required);
/* Install a fixed-size split ring before DRIVER_OK. The caller provides
   three exclusive DMA pages from this device's domain, already initialized
   and synchronized by vrinit. Keep them pinned through a confirmed stop. */
int vqueue(Vdev *device, u32 index, u64 desc, u64 avail, u64 used);
int vstart(Vdev *device);
/* Ring the doorbell after vrsubmit has made avail.idx visible. An error
   does not undo publication or permit DMA buffer reuse. */
int vnotify(Vdev *device, u32 index);
/* With the platform IRQ masked, sample and acknowledge device causes once.
   Zero means no current cause. Drain indicated work before rearming the
   platform IRQ. The snapshot is a hint, never proof of DMA completion. */
int vinterrupt(Vdev *device, u32 *causes);
#endif
