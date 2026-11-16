#include <hal/irq.h>
#include <hal/clock.h>
#include <diag.h>
static isize
sbicall(u64 extension, u64 function, u64 value)
{
  register u64 a0 __asm__("a0") = value;
  register u64 a1 __asm__("a1");
  register u64 a6 __asm__("a6") = function;
  register u64 a7 __asm__("a7") = extension;
  __asm__ __volatile__("ecall" : "+r"(a0), "=r"(a1) : "r"(a6), "r"(a7) : "memory");
  return a0 ? -1 : (isize) a1;
}
int
hal_timer_init(const Htimer *timer)
{
  UNUSED(timer);
  if (sbicall(0x10, 3, 0x54494d45) <= 0)
    return -1;
  if (sbicall(0x54494d45, 0, ~(u64) 0))
    return -1;
  __asm__ __volatile__("csrw sie, zero" ::: "memory");
  return 0;
}
void
hal_timer_set(u64 ticks)
{
  if (sbicall(0x54494d45, 0, ticks))
    panic("SBI timer programming");
  __asm__ __volatile__("csrs sie, %0" ::"r"(32UL) : "memory");
}
int
hal_timer_ack(void)
{
  u64 pending;
  __asm__ __volatile__("csrr %0, sip" : "=r"(pending));
  if (!(pending & 32))
    return 0;
  __asm__ __volatile__("csrc sie, %0" ::"r"(32UL) : "memory");
  if (sbicall(0x54494d45, 0, ~(u64) 0))
    panic("SBI timer acknowledgement");
  return 1;
}
u64
hal_irq_save(void)
{
  u64 state;
  __asm__ __volatile__("csrrc %0, sstatus, %1" : "=r"(state) : "r"(2UL) : "memory");
  return state & 2;
}
void
hal_irq_restore(u64 state)
{
  if (state & 2)
    __asm__ __volatile__("csrs sstatus, %0" ::"r"(2UL) : "memory");
  else
    __asm__ __volatile__("csrc sstatus, %0" ::"r"(2UL) : "memory");
}
