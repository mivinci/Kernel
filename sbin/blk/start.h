#ifndef _BLK_START_H
#define _BLK_START_H
#include <sys/boot.h>
#include <abi/event.h>
#include <abi/mem.h>
#include <abi/resource.h>
#include "io.h"
#include "transport.h"
#include "serve.h"
#include "private/virtio/mmio.h"

/* Private trusted-loader tags. They are not public Boot or block ABI. */
#define BLK_BOOT_OBSERVE UINT64_C(256)
#define BLK_BOOT_INIT    UINT64_C(257)
#define BLK_BOOT_MMIO    UINT64_C(258)
#define BLK_BOOT_IRQ     UINT64_C(259)
#define BLK_BOOT_DMA     UINT64_C(260)

#define BLK_DMA_PAGES     9
#define BLK_RING_PAGES    3
#define BLK_REQUEST_PAGES 6
#define BLK_REQUEST_COUNT 2
#define BLK_MMIO_BASE     UINT64_C(0x2000000)
#define BLK_DMA_BASE      UINT64_C(0x2100000)
#define BLK_CONN_BASE     UINT64_C(0x2300000)
#define BLK_READY_NS      UINT64_C(10000000000)
#define BLK_RETRY_NS      UINT64_C(1000000)

enum
{
  BlkSpace,
  BlkAlloc,
  BlkObserve,
  BlkInit,
  BlkMmio,
  BlkIrq,
  BlkDma,
  BlkHandles
};

typedef struct Blkstart Blkstart;
struct Blkstart
{
  Handle handle[BlkHandles];
  u64    device, mmiooffset, mmiobytes, irqevent;
  u64    addresses[BLK_DMA_PAGES];
  Handle pid;
  u64    image, initimage, number;
};

/* Private metadata arguments after the program name:
   image-number device-index mmio-page-offset mmio-bytes, followed by nine
   aligned DMA device addresses. Resource tags carry only handles. Unknown
   Boot entries stay available to future startup adapters. */
isize blkstartboot(const Bootview *, Blkstart *);
/* Check the current non-PID1 child and exact Local rights/windows before any
   mapping or device access. RESOURCE records do not replace kernel checks. */
isize blkstartcheck(Blkstart *);

/* Fresh CPU storage for the one-device production owner. */
typedef struct Blkstate Blkstate;
struct Blkstate
{
  Vdev         device;
  Vmmio        mmio;
  Vdevops      vops;
  Vblkconfig   config;
  Vring        ring;
  Vringops     ringops;
  Vblkreq      requests[BLK_REQUEST_COUNT];
  Blkdma       buffers[BLK_REQUEST_COUNT];
  Vblkhead    *headers[BLK_REQUEST_COUNT];
  volatile u8 *data[BLK_REQUEST_COUNT];
  volatile u8 *status[BLK_REQUEST_COUNT];
  void        *ringmapping[BLK_RING_PAGES];
  Blkqueue     queue;
  Filelink     links[1];
  Blkjob       jobs[BLK_REQUEST_COUNT];
  Blktransport transport;
  Blkservice   service;
};
STATIC_ASSERT(BLK_RING_PAGES + BLK_REQUEST_PAGES == BLK_DMA_PAGES, blk_dma_page_layout);
STATIC_ASSERT(BLK_REQUEST_COUNT *BLK_RING_PAGES == BLK_REQUEST_PAGES, blk_request_page_layout);
#endif
