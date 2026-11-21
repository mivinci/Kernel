#ifndef _VIRTIO_BLOCK_H
#define _VIRTIO_BLOCK_H
#include "ring.h"
#include "device.h"

#define VBLK_READ        0
#define VBLK_WRITE       1
#define VBLK_FLUSH       4
#define VBLK_OK          0
#define VBLK_IOERR       1
#define VBLK_UNSUPPORTED 2
#define VBLK_PENDING     255
#define VBLK_SECTOR      512
#define VBLK_DEVICE      2
#define VBLK_F_RO        (UINT64_C(1) << 5)
#define VBLK_F_BLK_SIZE  (UINT64_C(1) << 6)
#define VBLK_F_FLUSH     (UINT64_C(1) << 9)

typedef struct Vblkhead   Vblkhead;
typedef struct Vblkconfig Vblkconfig;
typedef struct Vblkreq    Vblkreq;
typedef int (*Vblksync)(void *context, u32 phase, const Vbuf *buffer);
struct Vblkhead
{
  u32 type, reserved;
  u64 sector;
};
STATIC_ASSERT(sizeof(Vblkhead) == 16 && offsetof(Vblkhead, sector) == 8, virtio_blk_header);
struct Vblkconfig
{
  u64 sectors;
  u32 readonly, flush, blocksize;
};
struct Vblkreq
{
  Vbuf         buffers[3];
  volatile u8 *status;
  Vblksync     sync;
  void        *context;
  u64          cookie, generation;
  u32          type, bytes, held, active;
};

int vblkfeatures(Vdev *device);
/* Take one generation-checked capacity and block size snapshot. A moving configuration
   returns WOULD_BLOCK without touching out. No FLUSH means read-only policy. */
int vblkconfig(const Vdev *device, Vblkconfig *out);

/* DMA address/domain and synchronization remain the transport owner's
   responsibility. The prepared header and status must occupy independent
   synchronized storage. Return the descriptor count or a native error. */
int vblkprepare(u32 type, u64 sector, u64 capacity, u64 header, u64 data, u32 bytes, u64 status,
                Vblkhead *wire, Vbuf buffers[3]);
/* Call only after used-ring validation and the payload/status DMA acquire.
   Return a device status, or PEER on malformed completion. */
int vblkresult(u32 type, u32 bytes, u32 written, u8 status);
/* Start with a zeroed request. The caller provides exclusive DMA storage and
   a valid mapping for wire/status, prepares write data, and keeps all pages
   pinned. sync maps a descriptor to its DMA window; writable selects the
   device-write direction. An error with held buffers requires recovery. */
int vblksubmit(Vblkreq *request, Vring *ring, const Vblkconfig *config, u32 type, u64 sector,
               u64 cookie, u64 header, u64 data, u32 bytes, u64 status, Vblkhead *wire,
               volatile u8 *flag, Vblksync sync, void *context);
/* Pass only a completion accepted by vrpoll for this ring. The cookie must
   be unique within the ring generation. Returns a device
   status or native error. Do not expose failed read data. */
int vblkfinish(Vblkreq *request, const Vdone *done);
/* Recovery only: caller already confirmed the device stopped. Take back
   windows still held by the request. Does not stop the ring or unpin pages. */
int vblkreclaim(Vblkreq *request);
#endif
