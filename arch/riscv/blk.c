#include <arch/riscv/virtio.h>
#include <blk.h>

int blk_read(unsigned long sector, void *buf) {
  return virtio_blk_read(sector, buf);
}

int blk_write(unsigned long sector, const void *buf) {
  return virtio_blk_write(sector, buf);
}

unsigned long blk_capacity(void) {
  return virtio_blk_capacity();
}