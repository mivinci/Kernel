#include <arch/riscv/pci.h>
#include <kernel.h>
#include <libc.h>

/*
 * Find a virtio-blk device on the PCI bus.
 * Returns 0 on success, -1 on failure.
 */
int pci_find_virtio_blk(int *bus, int *dev, int *func) {
  for (int b = 0; b <= PCI_BUS_MAX; b++) {
    for (int d = 0; d <= PCI_DEV_MAX; d++) {
      for (int f = 0; f <= PCI_FUNC_MAX; f++) {
        unsigned short vendor = pci_read16(b, d, f, PCI_VENDOR_ID);
        if (vendor == 0xffff) continue;

        unsigned short device = pci_read16(b, d, f, PCI_DEVICE_ID);
        if (vendor == VIRTIO_PCI_VENDOR &&
            (device == VIRTIO_PCI_DEV_BLK_TRANS || device == VIRTIO_PCI_DEV_BLK_MODERN)) {
          *bus  = b;
          *dev  = d;
          *func = f;
          printk("[pci] virtio-blk found at %d:%d.%d vendor=%x "
                 "device=%x\n",
                 b, d, f, vendor, device);
          return 0;
        }
      }
    }
  }
  return -1;
}

/*
 * Parse a PCI capability entry.
 */
int pci_read_cap(int bus, int dev, int func, int cap_off, struct PciCap *cap) {
  memset(cap, 0, sizeof(*cap));

  unsigned int addr = pci_config_addr(bus, dev, func, cap_off);

  cap->id   = *(volatile unsigned char *)(addr + 0);
  cap->next = *(volatile unsigned char *)(addr + 1);

  if (cap->id != VIRTIO_PCI_CAP_VENDOR) return -1;

  cap->cfg_type = *(volatile unsigned char *)(addr + 3);
  cap->bar      = *(volatile unsigned char *)(addr + 4);
  cap->offset   = *(volatile unsigned int *)(addr + 8);
  cap->length   = *(volatile unsigned int *)(addr + 12);

  return 0;
}

/*
 * Enable bus mastering for DMA.
 */
void pci_enable_bus_master(int bus, int dev, int func) {
  unsigned short cmd = pci_read16(bus, dev, func, PCI_COMMAND);
  cmd |= PCI_CMD_MASTER | PCI_CMD_MEM;
  pci_write32(bus, dev, func, PCI_COMMAND, cmd);
}
