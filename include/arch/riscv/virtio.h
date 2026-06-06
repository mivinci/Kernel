#ifndef _ARCH_RISCV_VIRTIO_H
#define _ARCH_RISCV_VIRTIO_H

#include <libc.h>

/* QEMU RISC-V virt machine: virtio-blk at MMIO 0x10001000 */
#define VIRTIO0     0x10001000UL
#define SECTOR_SIZE 512

/* Virtio MMIO register offsets */
#define VIRTIO_MAGIC            0x000
#define VIRTIO_VERSION          0x004
#define VIRTIO_DEVICE_ID        0x008
#define VIRTIO_VENDOR_ID        0x00c
#define VIRTIO_DEVICE_FEATURES  0x010
#define VIRTIO_DRIVER_FEATURES  0x020
#define VIRTIO_QUEUE_SEL        0x030
#define VIRTIO_QUEUE_NUM_MAX    0x034
#define VIRTIO_QUEUE_NUM        0x038
#define VIRTIO_QUEUE_READY      0x044
#define VIRTIO_QUEUE_NOTIFY     0x050
#define VIRTIO_INTR_STATUS      0x060
#define VIRTIO_INTR_ACK         0x064
#define VIRTIO_STATUS           0x070
#define VIRTIO_QUEUE_DESC_LOW   0x080
#define VIRTIO_QUEUE_DESC_HIGH  0x084
#define VIRTIO_DRIVER_DESC_LOW  0x090
#define VIRTIO_DRIVER_DESC_HIGH 0x094
#define VIRTIO_DEVICE_DESC_LOW  0x0a0
#define VIRTIO_DEVICE_DESC_HIGH 0x0a4

/* Virtio status bits */
#define VIRTIO_STATUS_ACK       (1 << 0)
#define VIRTIO_STATUS_DRIVER    (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK (1 << 2)
#define VIRTIO_STATUS_FEAT_OK   (1 << 3)
#define VIRTIO_STATUS_FAILED    (1 << 7)

/* Device IDs */
#define VIRTIO_ID_BLOCK 2

/* Descriptor flags */
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* Queue size (must be power of 2) */
#define VIRTQ_SIZE 8

/* Virtqueue descriptor */
XDEF_STRUCT(VRingDesc) {
  unsigned long  addr;
  unsigned int   len;
  unsigned short flags;
  unsigned short next;
};

/* Available ring */
XDEF_STRUCT(VRingAvail) {
  unsigned short flags;
  unsigned short idx;
  unsigned short ring[VIRTQ_SIZE];
};

/* Used ring entry */
XDEF_STRUCT(VRingUsedElem) {
  unsigned int id;
  unsigned int len;
};

/* Used ring */
XDEF_STRUCT(VRingUsed) {
  unsigned short flags;
  unsigned short idx;
  VRingUsedElem  ring[VIRTQ_SIZE];
};

/* Virtio block request header */
XDEF_STRUCT(VirtioBlkReq) {
  unsigned int  type; /* 0=read, 1=write */
  unsigned int  reserved;
  unsigned long sector;
};

/* Virtio block request types */
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

/* MMIO read/write helpers */
static inline unsigned int vread(unsigned long reg) {
  return *(volatile unsigned int *)(VIRTIO0 + reg);
}

static inline void vwrite(unsigned long reg, unsigned int val) {
  *(volatile unsigned int *)(VIRTIO0 + reg) = val;
}

void          virtio_blk_init(void);
int           virtio_blk_read(unsigned long sector, void *buf);
int           virtio_blk_write(unsigned long sector, const void *buf);
unsigned long virtio_blk_capacity(void);

#endif /* _ARCH_RISCV_VIRTIO_H */
