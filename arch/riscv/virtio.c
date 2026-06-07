#include <arch/riscv/pci.h>
#include <arch/riscv/virtio.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>

/*
 * virtio-blk driver with dual-mode (legacy + non-legacy) MMIO queue
 * setup.  Single-page ring allocation compatible with all QEMU versions
 * regardless of the force-legacy default.
 */

struct VirtioPciCfg {
  unsigned int   device_feature_select;
  unsigned int   device_feature;
  unsigned int   driver_feature_select;
  unsigned int   driver_feature;
  unsigned short queue_select;
  unsigned short queue_size;
  unsigned short queue_enable;
  unsigned short queue_notify_off;
  unsigned long  queue_desc;
  unsigned long  queue_avail;
  unsigned long  queue_used;
  unsigned short queue_ready;
  unsigned char  device_status;
};

static unsigned long  mmio_base;
static unsigned long *pci_bar;
static int            use_pci;

static VRingDesc     *descs;
static VRingAvail    *avail;
static VRingUsed     *used;
static unsigned long  capacity;
static unsigned short last_avail_idx;
static char           blk_buf[SECTOR_SIZE] __attribute__((aligned(16)));

static unsigned long pci_notify_addr;

static inline volatile struct VirtioPciCfg *pci_cfg(void) {
  return (volatile struct VirtioPciCfg *)pci_bar;
}
static inline void pci_set_status(unsigned char s) {
  pci_cfg()->device_status = s;
}
static inline void pci_notify(int qidx) {
  *(volatile unsigned short *)(pci_notify_addr + qidx * 2) = qidx;
}

static int virtio_pci_blk_init(void) {
  int bus, dev, func;
  if (pci_find_virtio_blk(&bus, &dev, &func) < 0) return -1;

  pci_enable_bus_master(bus, dev, func);

  /* Read BAR4 — pre-assigned by firmware, or assign to PCI-e window */
  unsigned int  bar4_lo   = pci_read32(bus, dev, func, PCI_BAR0 + 4 * 4);
  unsigned int  bar4_hi   = pci_read32(bus, dev, func, PCI_BAR0 + 4 * 4 + 4);
  unsigned long bar4_addr = ((unsigned long)bar4_hi << 32) | (bar4_lo & ~0xfUL);

  if (!bar4_addr) {
    bar4_addr = 0x40000000; /* PCI-e 32-bit MMIO window */
    pci_write32(bus, dev, func, PCI_BAR0 + 4 * 4, bar4_addr);
    pci_write32(bus, dev, func, PCI_BAR0 + 4 * 4 + 4, 0);
  }

  /* Parse PCI capabilities for virtio config offsets */
  unsigned char cap_ptr    = pci_read8(bus, dev, func, PCI_CAP_PTR);
  unsigned long common_off = 0, notify_off = 0, devcfg_off = 0;
  int           found_common = 0, found_notify = 0;

  while (cap_ptr) {
    unsigned int  ca  = pci_config_addr(bus, dev, func, cap_ptr);
    unsigned char id  = *(volatile unsigned char *)(unsigned long)ca;
    unsigned char nxt = *(volatile unsigned char *)(unsigned long)(ca + 1);

    if (id == VIRTIO_PCI_CAP_VENDOR) {
      unsigned char type = *(volatile unsigned char *)(unsigned long)(ca + 3);
      unsigned int  off  = *(volatile unsigned int *)(unsigned long)(ca + 8);

      if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
        common_off   = off;
        found_common = 1;
      } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        notify_off   = off;
        found_notify = 1;
      } else if (type == VIRTIO_PCI_CAP_DEVICE_CFG) {
        devcfg_off = off;
      }
    }
    cap_ptr = nxt;
  }

  if (!found_common || !found_notify) return -1;

  pci_bar         = (unsigned long *)(bar4_addr + common_off);
  pci_notify_addr = bar4_addr + notify_off;

  /* Init sequence: reset → ACK → DRIVER → FEAT_OK */
  pci_set_status(0);
  pci_set_status(VIRTIO_STATUS_ACK);
  pci_set_status(pci_cfg()->device_status | VIRTIO_STATUS_DRIVER);

  /* Feature negotiation: read all feature pages and accept everything */
  pci_cfg()->device_feature_select = 0;
  unsigned int feat_lo = pci_cfg()->device_feature;
  pci_cfg()->device_feature_select = 1;
  unsigned int feat_hi = pci_cfg()->device_feature;
  printk("[virtio-pci] features=%x:%x\n", feat_lo, feat_hi);

  pci_cfg()->driver_feature_select = 0;
  pci_cfg()->driver_feature        = feat_lo;
  pci_cfg()->driver_feature_select = 1;
  pci_cfg()->driver_feature        = feat_hi;
  pci_set_status(pci_cfg()->device_status | VIRTIO_STATUS_FEAT_OK);

  /* Read capacity */
  unsigned int cap_lo = *(volatile unsigned int *)(bar4_addr + devcfg_off);
  unsigned int cap_hi = *(volatile unsigned int *)(bar4_addr + devcfg_off + 4);
  capacity            = ((unsigned long)cap_hi << 32) | cap_lo;

  /* Allocate virtqueue rings */
  descs = (VRingDesc *)kalloc();
  avail = (VRingAvail *)kalloc();
  used  = (VRingUsed *)kalloc();
  memset(descs, 0, PAGE_SIZE);
  memset(avail, 0, PAGE_SIZE);
  memset(used, 0, PAGE_SIZE);

  /* Set up virtqueue 0 */
  pci_cfg()->queue_select = 0;
  pci_cfg()->queue_size   = VIRTQ_SIZE;
  pci_cfg()->queue_desc   = (unsigned long)descs;
  pci_cfg()->queue_avail  = (unsigned long)avail;
  pci_cfg()->queue_used   = (unsigned long)used;
  pci_cfg()->queue_ready  = 1;
  pci_set_status(pci_cfg()->device_status | VIRTIO_STATUS_DRIVER_OK);

  /* Read notify offset for queue 0 */
  unsigned short qnotify = pci_cfg()->queue_notify_off;
  pci_notify_addr += qnotify * 2;

  last_avail_idx = 0;
  use_pci        = 1;

  printk("[virtio-pci] ready, cap=%d MB (%d sectors)\n", (capacity / 2) / 1024, capacity);
  return 0;
}

static int virtio_mmio_blk_init(void) {
  unsigned long slots[] = {0x10001000, 0x10002000, 0x10003000, 0x10004000,
                           0x10005000, 0x10006000, 0x10007000, 0x10008000};
  for (int i = 0; i < 8; i++) {
    unsigned long base = slots[i];
    if (*(volatile unsigned int *)(base) != 0x74726976) continue;

    *(volatile unsigned int *)(base + VIRTIO_STATUS) = 0;
    *(volatile unsigned int *)(base + VIRTIO_STATUS) |= VIRTIO_STATUS_ACK;

    if (*(volatile unsigned int *)(base + VIRTIO_DEVICE_ID) != VIRTIO_ID_BLOCK) continue;

    mmio_base = base;
    break;
  }

  if (!mmio_base) return -1;

  /*
   * Feature negotiation: read device features, reject complex ones
   * like event_idx and indirect_desc for simpler I/O path.
   */
  unsigned int feat_lo =
      *(volatile unsigned int *)(mmio_base + VIRTIO_DEVICE_FEATURES);
  unsigned int feat_hi = 0;
  if (feat_lo & 0x80000000) {
    *(volatile unsigned int *)(mmio_base + 0x014) = 1;
    feat_hi = *(volatile unsigned int *)(mmio_base + VIRTIO_DEVICE_FEATURES);
    *(volatile unsigned int *)(mmio_base + 0x014) = 0;
  }
  printk("[virtio-mmio] features=%x:%x\n", feat_lo, feat_hi);

  /* Reject complex features for simple I/O */
  feat_lo &= ~(1UL << 24); /* VIRTIO_F_NOTIFY_ON_EMPTY */
  feat_lo &= ~(1UL << 27); /* VIRTIO_F_ANY_LAYOUT */
  feat_lo &= ~(1UL << 28); /* VIRTIO_F_RING_EVENT_IDX */
  feat_lo &= ~(1UL << 29); /* VIRTIO_F_RING_INDIRECT */
  /* Accept VIRTIO_F_VERSION_1 for non-legacy queue setup.
   * Without this, QEMU stays in legacy mode and ignores
   * QUEUE_DESC/AVAIL/USED register writes — causing I/O hang. */

  /* Set DRIVER first */
  *(volatile unsigned int *)(mmio_base + VIRTIO_STATUS) |=
      VIRTIO_STATUS_DRIVER;

  /* Write driver features — both pages for non-legacy */
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_FEATURES_SEL) = 0;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_FEATURES) =
      feat_lo;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_FEATURES_SEL) = 1;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_FEATURES) =
      feat_hi;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_FEATURES_SEL) = 0;

  /* Set FEAT_OK */
  *(volatile unsigned int *)(mmio_base + VIRTIO_STATUS) |=
      VIRTIO_STATUS_FEAT_OK;

  /* Verify FEAT_OK */
  unsigned int st =
      *(volatile unsigned int *)(mmio_base + VIRTIO_STATUS);
  if (!(st & VIRTIO_STATUS_FEAT_OK))
    printk("[virtio-mmio] FEAT_OK not accepted (status=%x)\n", st);

  unsigned int cl = *(volatile unsigned int *)(mmio_base + 0x100);
  unsigned int ch = *(volatile unsigned int *)(mmio_base + 0x104);
  capacity        = ((unsigned long)ch << 32) | cl;

  /*
   * Single-page ring layout (all fit in 4KB). VIRTQ_SIZE=8:
   *   descs at offset   0 (128 bytes)
   *   avail at offset 128 ( 20 bytes)
   *   used  at offset 160 ( 68 bytes, 16-byte aligned)
   * This layout works with both legacy QUEUE_PFN and non-legacy
   * QUEUE_DESC/AVAIL/USED register-based queue setup.
   */
  char         *ring_page = (char *)kalloc();
  unsigned long ring_pa   = (unsigned long)ring_page;
  memset(ring_page, 0, PAGE_SIZE);

  descs = (VRingDesc *)(ring_page + 0);
  avail = (VRingAvail *)(ring_page + 128);
  used  = (VRingUsed *)(ring_page + 160);

  /*
   * Dual-mode queue setup: write both legacy and non-legacy
   * registers.  QEMU ignores the ones that don't match its
   * mode (force-legacy property).
   */
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_SEL)   = 0;
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_ALIGN) = 16;
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_NUM)   = VIRTQ_SIZE;

  /* Non-legacy addresses */
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_DESC_LOW)   = (ring_pa + 0) & 0xffffffff;
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_DESC_HIGH)  = (ring_pa + 0) >> 32;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_DESC_LOW)  = (ring_pa + 128) & 0xffffffff;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DRIVER_DESC_HIGH) = (ring_pa + 128) >> 32;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DEVICE_DESC_LOW)  = (ring_pa + 160) & 0xffffffff;
  *(volatile unsigned int *)(mmio_base + VIRTIO_DEVICE_DESC_HIGH) = (ring_pa + 160) >> 32;

  /* Legacy queue setup */
  *(volatile unsigned int *)(mmio_base + VIRTIO_GUEST_PAGE_SIZE) = PAGE_SIZE;
  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_PFN)       = ring_pa >> 12;

  *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_READY) = 1;
  *(volatile unsigned int *)(mmio_base + VIRTIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;

  last_avail_idx = 0;
  use_pci        = 0;

  printk("[virtio-mmio] ready, cap=%d MB (%d sectors)\n", (capacity / 2) / 1024, capacity);
  return 0;
}

void virtio_blk_init(void) {
  if (virtio_pci_blk_init() == 0) return;
  if (virtio_mmio_blk_init() == 0) return;
  printk("[virtio] no block device found\n");
}

static int blk_request(int type, unsigned long sector, void *buf) {
  if ((!use_pci && !mmio_base) || (use_pci && !pci_bar) || !descs) return -1;

  static VirtioBlkReq  req __attribute__((aligned(16)));
  static unsigned char status __attribute__((aligned(4)));
  req.type   = type;
  req.sector = sector;

  descs[0].addr  = (unsigned long)&req;
  descs[0].len   = sizeof(req);
  descs[0].flags = VRING_DESC_F_NEXT;
  descs[0].next  = 1;
  descs[1].addr  = (unsigned long)buf;
  descs[1].len   = SECTOR_SIZE;
  descs[1].flags = VRING_DESC_F_NEXT |
                   ((type == VIRTIO_BLK_T_IN) ? VRING_DESC_F_WRITE : 0);
  descs[1].next  = 2;
  descs[2].addr  = (unsigned long)&status;
  descs[2].len   = 1;
  descs[2].flags = VRING_DESC_F_WRITE;
  descs[2].next  = 0;

  avail->ring[avail->idx % VIRTQ_SIZE] = 0;
  __asm__ __volatile__("fence w,w" ::: "memory");
  avail->idx += 1;
  __asm__ __volatile__("fence w,w" ::: "memory");

  if (use_pci)
    pci_notify(0);
  else {
    *(volatile unsigned int *)(mmio_base + VIRTIO_QUEUE_NOTIFY) = 0;
  }

  for (int retry = 0; retry < 1000000; retry++) {
    if (used->idx != last_avail_idx) break;
    __asm__ __volatile__("" ::: "memory");
  }
  if (used->idx == last_avail_idx) return -1;

  __asm__ __volatile__("fence r,r" ::: "memory");
  last_avail_idx++;
  return (status == 0) ? 0 : -1;
}

int virtio_blk_read(unsigned long sector, void *buf) {
  return blk_request(VIRTIO_BLK_T_IN, sector, buf);
}

int virtio_blk_write(unsigned long sector, const void *buf) {
  if ((!use_pci && !mmio_base) || (use_pci && !pci_bar)) return -1;
  memcpy(blk_buf, buf, SECTOR_SIZE);
  return blk_request(VIRTIO_BLK_T_OUT, sector, blk_buf);
}

unsigned long virtio_blk_capacity(void) {
  return capacity;
}
