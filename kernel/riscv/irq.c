#include <hal/irq.h>
#include <hal/clock.h>
#include <diag.h>
static u64 priorities, enables, context, active;
static u32 limit, allowed[32];
static void
barrier(void)
{
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}
static u32
read32(u64 address)
{
  return *(volatile u32 *) address;
}
static void
write32(u64 address, u32 value)
{
  *(volatile u32 *) address = value;
}
int
hal_irq_init(const Hirq *controller, const u64 mapped[HIRQ_WINDOWS])
{
  u64 physical[HIRQ_WINDOWS];
  u32 i;
  if (priorities || !mapped || hal_irq_pages(controller, physical) != 3 || !mapped[0] ||
      !mapped[1] || !mapped[2] || ((mapped[0] | mapped[1] | mapped[2]) & PAGE_MASK))
    return -1;
  for (i = 0; i < controller->count; i++)
    if (!controller->source[i].number || controller->source[i].number > controller->limit ||
        controller->source[i].flags)
      return -1;
  __asm__ __volatile__("csrc sie, %0" ::"r"(512UL) : "memory");
  for (i = 0; i < (controller->limit + 32) / 32; i++)
    write32(mapped[2] + ((0x2000UL + (u64) controller->context * 0x80) & PAGE_MASK) + 4 * i, 0);
  for (i = 1; i <= controller->limit; i++)
    write32(mapped[0] + 4 * i, 0);
  write32(mapped[1], 0);
  for (i = 0; i < controller->count; i++)
    allowed[controller->source[i].number / 32] |= 1U << (controller->source[i].number % 32);
  barrier();
  priorities = mapped[0];
  context = mapped[1];
  enables = mapped[2] + ((0x2000UL + (u64) controller->context * 0x80) & PAGE_MASK);
  limit = controller->limit;
  __asm__ __volatile__("csrs sie, %0" ::"r"(512UL) : "memory");
  return 0;
}
u64
hal_irq_claim(void)
{
  u32 number;
  if (!context || active)
    panic("IRQ claim order");
  barrier();
  number = read32(context + 4);
  barrier();
  if (!number)
    return 0;
  if (number > limit)
    panic("IRQ claimed number");
  active = (u64) number + 1;
  return active;
}
u32
hal_irq_number(u64 token)
{
  return token ? (u32) (token - 1) : 0xffffffffU;
}
void
hal_irq_mask(u32 number)
{
  if (!priorities || !number || number > limit)
    panic("IRQ mask number");
  /* PLIC completion ignores disabled sources. Priority zero masks delivery
     while retaining the context enable required for the following complete. */
  write32(priorities + 4 * number, 0);
  barrier();
}
void
hal_irq_complete(u64 token)
{
  if (!context || !token || token != active)
    panic("IRQ completion order");
  barrier();
  write32(context + 4, hal_irq_number(token));
  barrier();
  active = 0;
}
void
hal_irq_unmask(u32 number)
{
  u64 address;
  if (!priorities || !number || number > limit || !(allowed[number / 32] & (1U << (number % 32))))
    panic("IRQ unmask number");
  address = enables + 4 * (number / 32);
  write32(address, read32(address) | (1U << (number % 32)));
  barrier();
  write32(priorities + 4 * number, 1);
  barrier();
}
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
  __asm__ __volatile__("csrc sie, %0" ::"r"(32UL) : "memory");
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
