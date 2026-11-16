#ifndef _IRQ_H
#define _IRQ_H
#include <dtb.h>
#include <hal/irq.h>
#include <image.h>
#include <event.h>
#include <abi/irq.h>
#ifndef IRQS
#define IRQS 64
#endif
#define IRQ_DISABLED  1
#define IRQ_ARMED     2
#define IRQ_DELIVERED 3
#define IRQ_REVOKED   4
typedef struct Irqid   Irqid;
typedef struct Irqstat Irqstat;
struct Irqid
{
  u64 generation;
  u32 index, reserved;
};
struct Irqstat
{
  Imageid image;
  Handle  owner;
  u32     number, bit, state, reserved;
};
/* Hardware description is not authority. Inventory is immutable after init. */
int irqdescribe(const Dtinfo *info, Hirq *out, Hirqsource *storage, u32 capacity);
int irqinit(const Hirq *controller);
int irqsource(u32 number, u32 *out);
/* Trusted typed construction; the single returned ref must become a Local. */
int  irqnew(u32 source, Imageid image, Event *event, u32 bit, Irqid *out);
int  irqack(Irqid id, Imageid image);
int  irqcancel(Irqid id, Imageid image);
int  irqdrop(Irqid id);
void irqend(Imageid image);
/* Owns mask/complete. Positive result posts the exact image's persistent bit. */
int irqfire(u64 token, Imageid *out);
int irqwaiting(void);
int irqstat(Irqid id, Irqstat *out);
int irqcheck(void);
#endif
