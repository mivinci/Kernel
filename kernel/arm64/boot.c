#include <boot.h>
#if BOOT_CASE == 1
#include <traptest.h>
#endif
#include <diag.h>
#include <hal/boot.h>
#include <arm64/trap.h>
#include <abi/bootpkg.h>
void
hal_debug_sync(void)
{
  __asm__ __volatile__("dsb sy" ::: "memory");
}
extern char vectors[];
u64
hal_boot_machine(void)
{
  return BOOTPKG_ARM64;
}
void
arch_start(u64 dtb)
{
  u64 mode, mask, vm, cpu;
  __asm__ __volatile__("mrs %0, CurrentEL" : "=r"(mode));
  __asm__ __volatile__("mrs %0, daif" : "=r"(mask));
  __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(vm));
  __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(cpu));
  if (mode != 4)
    hal_halt();
  __asm__ __volatile__("msr vbar_el1, %0; isb" ::"r"(vectors) : "memory");
  main(cpu & 0xff00ffffffUL, dtb, mode, (mask & 0x3c0) == 0x3c0, vm & 1);
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
  puthex(frame->sp);
  putstr("\n");
#if BOOT_CASE == 1
  arch_test_trap(frame);
#else
  panic("exception");
#endif
}
