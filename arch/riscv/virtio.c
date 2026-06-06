#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <arch/riscv/virtio.h>

/* Per-device access helpers */
static inline unsigned int vr(unsigned long off) {
  extern unsigned long __virtio_mmio_base;
  return *(volatile unsigned int *)(__virtio_mmio_base + off);
}
static inline void vw(unsigned long off, unsigned int val) {
  extern unsigned long __virtio_mmio_base;
  *(volatile unsigned int *)(__virtio_mmio_base + off) = val;
}

unsigned long __virtio_mmio_base;
static VRingDesc  *descs;
static VRingAvail *avail;
static VRingUsed  *used;
static unsigned long capacity;
static unsigned short last_avail_idx;
static char blk_buf[SECTOR_SIZE] __attribute__((aligned(16)));

void virtio_blk_init(void) {
  unsigned long slots[] = {0x10001000, 0x10002000, 0x10003000,
                           0x10004000, 0x10005000, 0x10006000,
                           0x10007000, 0x10008000};
  for (int i = 0; i < 8; i++) {
    unsigned long base = slots[i];
    if (*(volatile unsigned int *)(base) != 0x74726976)
      continue;

    *(volatile unsigned int *)(base + VIRTIO_STATUS) = 0;
    *(volatile unsigned int *)(base + VIRTIO_STATUS) |=
        VIRTIO_STATUS_ACK;

    if (*(volatile unsigned int *)(base + VIRTIO_DEVICE_ID) !=
        VIRTIO_ID_BLOCK)
      continue;

    __virtio_mmio_base = base;
    break;
  }

  if (!__virtio_mmio_base) {
    printk("[virtio] no block device found\n");
    return;
  }

  /* Feature negotiation: read features (for future use) */
  unsigned int feat_lo =
      *(volatile unsigned int *)(__virtio_mmio_base + 0x010);
  if (feat_lo & 0x80000000) {
    *(volatile unsigned int *)(__virtio_mmio_base + 0x014) = 1;
    *(volatile unsigned int *)(__virtio_mmio_base + 0x014) = 0;
  }

  /* Negotiate: accept nothing (legacy mode) */
  *(volatile unsigned int *)(__virtio_mmio_base + 0x024) = 0;
  vw(VIRTIO_DRIVER_FEATURES, 0);

  vw(VIRTIO_STATUS,
     vr(VIRTIO_STATUS) | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEAT_OK);

  /* Read capacity */
  unsigned int cl =
      *(volatile unsigned int *)(__virtio_mmio_base + 0x100);
  unsigned int ch =
      *(volatile unsigned int *)(__virtio_mmio_base + 0x104);
  capacity = ((unsigned long)ch << 32) | cl;

  /* Allocate virtqueue rings on separate pages */
  descs = (VRingDesc *)kalloc();
  avail = (VRingAvail *)kalloc();
  used  = (VRingUsed *)kalloc();
  memset(descs, 0, PAGE_SIZE);
  memset(avail, 0, PAGE_SIZE);
  memset(used, 0, PAGE_SIZE);

  /* Set up virtqueue 0 */
  vw(VIRTIO_QUEUE_SEL, 0);
  vw(VIRTIO_QUEUE_NUM, VIRTQ_SIZE);

  unsigned long pa = (unsigned long)descs;
  vw(VIRTIO_QUEUE_DESC_LOW, pa & 0xffffffff);
  vw(VIRTIO_QUEUE_DESC_HIGH, pa >> 32);
  pa = (unsigned long)avail;
  vw(VIRTIO_DRIVER_DESC_LOW, pa & 0xffffffff);
  vw(VIRTIO_DRIVER_DESC_HIGH, pa >> 32);
  pa = (unsigned long)used;
  vw(VIRTIO_DEVICE_DESC_LOW, pa & 0xffffffff);
  vw(VIRTIO_DEVICE_DESC_HIGH, pa >> 32);

  vw(VIRTIO_QUEUE_READY, 1);
  vw(VIRTIO_STATUS,
     vr(VIRTIO_STATUS) | VIRTIO_STATUS_DRIVER_OK);
  last_avail_idx = 0;

  printk("[virtio] blk ready, cap=%d MB (%d sectors)\n",
         (capacity / 2) / 1024, capacity);
}

static int blk_request(int type, unsigned long sector, void *buf) {
  if (!__virtio_mmio_base || !descs)
    return -1;

  static VirtioBlkReq req __attribute__((aligned(16)));
  static unsigned char status __attribute__((aligned(4)));
  req.type   = type;
  req.sector = sector;

  /* Build descriptor chain: header → data → status */
  descs[0].addr  = (unsigned long)&req;
  descs[0].len   = sizeof(req);
  descs[0].flags = VRING_DESC_F_NEXT;
  descs[0].next  = 1;
  descs[1].addr  = (unsigned long)buf;
  descs[1].len   = SECTOR_SIZE;
  descs[1].flags = (type == VIRTIO_BLK_T_IN) ? VRING_DESC_F_WRITE : 0;
  descs[1].next  = 2;
  descs[2].addr  = (unsigned long)&status;
  descs[2].len   = 1;
  descs[2].flags = VRING_DESC_F_WRITE;
  descs[2].next  = 0;

  /* Submit to device */
  avail->ring[avail->idx % VIRTQ_SIZE] = 0;
  __asm__ __volatile__("fence w,w" ::: "memory");
  avail->idx += 1;
  __asm__ __volatile__("fence w,w" ::: "memory");
  vw(VIRTIO_QUEUE_NOTIFY, 0);

  /* Poll for completion (timeout after 1M iterations) */
  for (int retry = 0; retry < 1000000; retry++) {
    if (used->idx != last_avail_idx)
      break;
    __asm__ __volatile__("" ::: "memory");
  }
  if (used->idx == last_avail_idx)
    return -1;

  __asm__ __volatile__("fence r,r" ::: "memory");
  last_avail_idx++;
  return (status == 0) ? 0 : -1;
}

int virtio_blk_read(unsigned long sector, void *buf) {
  return blk_request(VIRTIO_BLK_T_IN, sector, buf);
}

int virtio_blk_write(unsigned long sector, const void *buf) {
  if (!__virtio_mmio_base)
    return -1;
  memcpy(blk_buf, buf, SECTOR_SIZE);
  return blk_request(VIRTIO_BLK_T_OUT, sector, blk_buf);
}

unsigned long virtio_blk_capacity(void) {
  return capacity;
}
