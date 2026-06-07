#ifndef _ARCH_RISCV_PCI_H
#define _ARCH_RISCV_PCI_H

#include <libc.h>

/* QEMU RISC-V virt: PCI-e ECAM at 0x30000000, bus 0 */
#define PCI_ECAM_BASE 0x30000000UL
#define PCI_BUS_MAX   0
#define PCI_DEV_MAX   31
#define PCI_FUNC_MAX  7

/* PCI config space offsets */
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND   0x04
#define PCI_STATUS    0x06
#define PCI_CLASS     0x0b
#define PCI_SUBCLASS  0x0a
#define PCI_PROG_IF   0x09
#define PCI_HDR_TYPE  0x0e
#define PCI_BAR0      0x10
#define PCI_CAP_PTR   0x34

/* PCI header type */
#define PCI_HDR_MULTIFUNC 0x80

/* PCI command bits */
#define PCI_CMD_IO     0x01
#define PCI_CMD_MEM    0x02
#define PCI_CMD_MASTER 0x04

/* Virtio PCI vendor/device IDs */
#define VIRTIO_PCI_VENDOR         0x1AF4
#define VIRTIO_PCI_DEV_BLK_TRANS  0x1001
#define VIRTIO_PCI_DEV_BLK_MODERN 0x1042

/* Virtio PCI capability IDs */
#define VIRTIO_PCI_CAP_VENDOR 0x09

/* Virtio PCI cfg types */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

/* ECAM access: get config space address */
static inline unsigned long pci_config_addr(int bus, int dev, int func, int off) {
  return PCI_ECAM_BASE + (((unsigned long)bus << 20) | ((unsigned long)dev << 15) |
                          ((unsigned long)func << 12) | (unsigned long)off);
}

static inline unsigned char pci_read8(int bus, int dev, int func, int off) {
  unsigned long addr = pci_config_addr(bus, dev, func, off);
  return *(volatile unsigned char *)(unsigned long)addr;
}

static inline unsigned short pci_read16(int bus, int dev, int func, int off) {
  unsigned long addr = pci_config_addr(bus, dev, func, off);
  return *(volatile unsigned short *)(unsigned long)addr;
}

static inline unsigned int pci_read32(int bus, int dev, int func, int off) {
  unsigned long addr = pci_config_addr(bus, dev, func, off);
  return *(volatile unsigned int *)(unsigned long)addr;
}

static inline void pci_write32(int bus, int dev, int func, int off, unsigned int val) {
  unsigned long addr = pci_config_addr(bus, dev, func, off);
  *(volatile unsigned int *)(unsigned long)addr = val;
}

/* PCI capability iteration */
struct PciCap {
  unsigned char id;
  unsigned char next;
  unsigned char cfg_type; /* virtio-pci specific */
  unsigned char bar;
  unsigned int  offset;
  unsigned int  length;
};

int  pci_init(void);
int  pci_find_virtio_blk(int *bus, int *dev, int *func);
int  pci_read_cap(int bus, int dev, int func, int cap_off, struct PciCap *cap);
void pci_enable_bus_master(int bus, int dev, int func);

#endif /* _ARCH_RISCV_PCI_H */
