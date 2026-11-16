#ifndef _HAL_IRQ_H
#define _HAL_IRQ_H
#include <types.h>
typedef struct Htimer Htimer;
struct Htimer
{
  u64 distributor, cpu;
  u32 irqs[4], flags[4], count, reserved;
};
/* Boot setup while IRQs are masked. Controller mappings have device attributes. */
int hal_timer_init(const Htimer *timer);
/* Mask/ack the timer source before returning; negative means an unexpected IRQ. */
int  hal_timer_ack(void);
void hal_irq_enable(void);
/* CPU stack, no lock, IRQs masked on entry and return. */
void hal_idle(void);
u64  hal_irq_save(void);
void hal_irq_restore(u64 state);
#endif
