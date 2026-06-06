#include <arch/riscv/csr.h>
#include <arch/riscv/plic.h>
#include <kernel.h>
#include <uart.h>

void plic_init(void) {
  unsigned long hartid = csr_read(mhartid);

  /* Set UART0 interrupt priority to 1 */
  plic_write(PLIC_PRIORITY + UART0_IRQ * 4, 1);

  /* Enable UART0 interrupt in PLIC for this hart's M-mode context */
  unsigned long en_addr = PLIC_ENABLE(hartid, PLIC_MODE_M) + (UART0_IRQ / 32) * 4;
  plic_write(en_addr, plic_read(en_addr) | (1U << (UART0_IRQ % 32)));

  /* Set threshold to 0 (accept all interrupts) */
  plic_write(PLIC_THRESHOLD(hartid, PLIC_MODE_M), 0);

  /* Enable machine external interrupt in mie */
  csr_write(mie, csr_read(mie) | MIE_MEIE);

  printk("[plic] hart=%d threshold=0\n", hartid);
}

void plic_handle(void) {
  unsigned long hartid = csr_read(mhartid);

  /* Claim the highest-priority pending interrupt */
  unsigned long irq = plic_read(PLIC_CLAIM(hartid, PLIC_MODE_M));

  if (irq == 0)
    return; /* Spurious */

  if (irq == UART0_IRQ) {
    uart_handle();
    plic_write(PLIC_CLAIM(hartid, PLIC_MODE_M), irq);
  } else {
    printk("[plic] unhandled irq=%d\n", irq);
    plic_write(PLIC_CLAIM(hartid, PLIC_MODE_M), irq);
  }
}
