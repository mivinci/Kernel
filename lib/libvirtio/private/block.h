#ifndef _VIRTIO_BLOCK_H
#define _VIRTIO_BLOCK_H
#include "ring.h"

#define VBLK_READ        0
#define VBLK_WRITE       1
#define VBLK_FLUSH       4
#define VBLK_OK          0
#define VBLK_IOERR       1
#define VBLK_UNSUPPORTED 2
#define VBLK_PENDING     255
#define VBLK_SECTOR      512

typedef struct Vblkhead Vblkhead;
struct Vblkhead
{
  u32 type, reserved;
  u64 sector;
};
STATIC_ASSERT(sizeof(Vblkhead) == 16 && offsetof(Vblkhead, sector) == 8, virtio_blk_header);

/* DMA address/domain and synchronization remain the transport owner's
   responsibility. The prepared header and status must occupy independent
   synchronized storage. Return the descriptor count or a native error. */
int vblkprepare(u32 type, u64 sector, u64 capacity, u64 header, u64 data, u32 bytes, u64 status,
                Vblkhead *wire, Vbuf buffers[3]);
/* Call only after used-ring validation and the payload/status DMA acquire.
   Return a device status, or PEER on malformed completion. */
int vblkresult(u32 type, u32 bytes, u32 written, u8 status);
#endif
