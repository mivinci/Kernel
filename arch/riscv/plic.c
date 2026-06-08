#include <arch/riscv/csr.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/virtio.h>
#include <kernel.h>
#include <uart.h>

unsigned long plic_mmio_base = 0x0c000000UL;

void plic_init(void) {
  unsigned long hartid = csr_read(mhartid);

  /* UART0 */
  plic_write(PLIC_PRIORITY + UART0_IRQ * 4, 1);
  unsigned long uen = PLIC_ENABLE(hartid, PLIC_MODE_M) + (UART0_IRQ / 32) * 4;
  plic_write(uen, plic_read(uen) | (1U << (UART0_IRQ % 32)));

  /* Virtio block device */
  plic_write(PLIC_PRIORITY + VIRTIO_IRQ * 4, 1);
  unsigned long ven = PLIC_ENABLE(hartid, PLIC_MODE_M) + (VIRTIO_IRQ / 32) * 4;
  plic_write(ven, plic_read(ven) | (1U << (VIRTIO_IRQ % 32)));

  /* Set threshold to 0 */
  plic_write(PLIC_THRESHOLD(hartid, PLIC_MODE_M), 0);
  csr_write(mie, csr_read(mie) | MIE_MEIE);

  printk("[plic] hart=%d threshold=0\n", hartid);
}

void plic_handle(void) {
  unsigned long hartid = csr_read(mhartid);
  unsigned long irq    = plic_read(PLIC_CLAIM(hartid, PLIC_MODE_M));

  if (irq == 0) return;

  /* Sanity check: a valid PLIC IRQ fits in 6 bits (0..63).
   * If we read garbage the plic_mmio_base may have been
   * corrupted by a stack overflow — drop the spurious claim
   * so we don't write back a bogus value. */
  if (irq > 63) {
    return;
  }

  if (irq == UART0_IRQ) {
    uart_handle();
  } else if (irq == VIRTIO_IRQ) {
    virtio_blk_intr_handler();
  } else {
    /* puts() avoids the 1024-byte vsprintf buffer on the trap stack */
    puts("[plic] unhandled irq=");
    putc('0' + (char)(irq / 10));
    if (irq >= 10) putc('0' + (char)(irq % 10));
    puts("\n");
  }
  plic_write(PLIC_CLAIM(hartid, PLIC_MODE_M), irq);
}