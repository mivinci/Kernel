/* Boot hardware description only; not IRQ authority or a driver binding. */
#ifndef _IRQ_H
#define _IRQ_H
#include <dtb.h>
#include <hal/irq.h>
int irqdescribe(const Dtinfo *info, Hirq *out, Hirqsource *storage, u32 capacity);
#endif
