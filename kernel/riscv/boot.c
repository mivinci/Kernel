#include <boot.h>
#if BOOT_CASE == 1
#include <traptest.h>
#endif
#include <diag.h>
#include <hal/boot.h>
#include <riscv/trap.h>
void
arch_start(u64 cpu, u64 dtb)
{
  u64 status, vm;
  __asm__ __volatile__("csrr %0, sstatus" : "=r"(status));
  __asm__ __volatile__("csrr %0, satp" : "=r"(vm));
  main(cpu, dtb, 1, (status & 2) == 0, vm);
}

void
arch_trap(Trapframe *frame)
{
  if ((u64) frame != (u64) __emergency_top - sizeof(*frame)) {
    panic("trap stack");
  }
  putstr("TRAP cause=");
  puthex(frame->cause);
  putstr(" pc=");
  puthex(frame->pc);
  putstr(" address=");
  puthex(frame->address);
  putstr(" sp=");
  puthex(frame->x[2]);
  putstr("\n");
#if BOOT_CASE == 1
  arch_test_trap(frame);
#else
  panic("exception");
#endif
}
