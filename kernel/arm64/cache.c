#include <hal/vm.h>
#include <vm.h>
void
hal_code_sync(u64 pa, u64 bytes)
{
  u64 ctr, line, address, end;
  __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
  line = 4UL << ((ctr >> 16) & 15);
  address = (u64) kaddr(pa);
  end = address + bytes;
  for (address = ALIGN_DOWN(address, line); address < end; address += line)
    __asm__ __volatile__("dc cvau, %0" ::"r"(address) : "memory");
  __asm__ __volatile__("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
}
