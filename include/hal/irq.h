#ifndef _HAL_IRQ_H
#define _HAL_IRQ_H
#include <types.h>
#define HIRQ_SOURCES 64
#define HIRQ_WINDOWS 3
#define HIRQ_PLIC    1
#define HIRQ_GIC     2
typedef struct Hirqsource Hirqsource;
typedef struct Hirq       Hirq;
typedef struct Htimer     Htimer;
struct Hirqsource
{
  u32 number, flags;
};
struct Hirq
{
  u64               base[2], bytes[2];
  const Hirqsource *source;
  u32               kind, regions, count, limit, context, reserved;
};
struct Htimer
{
  u32 irqs[4], flags[4], count, reserved;
};
STATIC_ASSERT(sizeof(Hirqsource) == 8, irq_source_layout);
STATIC_ASSERT(sizeof(Hirq) == 64, irq_descriptor_layout);
/* Boot-only descriptors. pages returns a count or -1, without partial output.
   init borrows source only for the call; mapped windows have device attributes. */
int hal_irq_pages(const Hirq *controller, u64 pages[HIRQ_WINDOWS]);
int hal_irq_init(const Hirq *controller, const u64 mapped[HIRQ_WINDOWS]);
/* IRQs masked, single CPU, no nested claims. Token 0 is empty; IRQ 0 is not. */
u64  hal_irq_claim(void);
u32  hal_irq_number(u64 token);
void hal_irq_mask(u32 number);
void hal_irq_complete(u64 token);
void hal_irq_unmask(u32 number);
int  hal_timer_init(const Htimer *timer);
/* Ack a timer if present. A negative result defers a non-timer claim, which
   the caller must immediately take via hal_irq_claim and complete. */
int  hal_timer_ack(void);
void hal_irq_enable(void);
/* CPU stack, no lock, IRQs masked on entry and return. */
void hal_idle(void);
u64  hal_irq_save(void);
void hal_irq_restore(u64 state);
#endif
