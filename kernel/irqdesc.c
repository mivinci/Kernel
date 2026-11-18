#include <irq.h>
int
irqdescribe(const Dtinfo *info, u64 cpu, Hirq *out, Hirqsource *storage, u32 capacity)
{
  const Dtdev *controller = NULL, *device;
  Hirqsource   sources[HIRQ_SOURCES];
  u32          i, j, irq, count = 0, intc = 0, found = 0, context = 0, selected = 0;
  if (!info || !out || !info->cpus || info->cpus > DT_CPUS || info->ndev > DT_DEVICES ||
      capacity > HIRQ_SOURCES)
    return DT_BAD;
  for (i = 0; i < info->cpus; i++)
    if (info->cpu[i].id == cpu) {
      intc = info->cpu[i].intc;
      found++;
    }
  if (found != 1)
    return DT_CONFLICT;
  for (i = 0; i < info->ndev; i++)
    if (info->dev[i].kind == DT_PLIC || info->dev[i].kind == DT_GIC) {
      if (controller)
        return DT_UNSUPPORTED;
      controller = &info->dev[i];
    }
  if (!controller || !controller->phandle ||
      (controller->kind == DT_GIC
           ? ((controller->nreg != 2 && controller->nreg != 4) || controller->nirq)
           : (controller->nreg != 1 || !controller->nirq || controller->nirq > DT_IRQS)))
    return DT_UNSUPPORTED;
  if (controller->kind == DT_PLIC) {
    if (!intc)
      return DT_CONFLICT;
    for (i = 0; i < controller->nirq; i++) {
      if (!controller->irq[i].parent || controller->irq[i].reserved)
        return DT_CONFLICT;
      if (controller->irq[i].parent == intc && controller->irq[i].number == 9) {
        context = i;
        selected++;
      }
    }
    if (selected != 1)
      return DT_CONFLICT;
  }
  for (i = 0; i < info->ndev; i++) {
    device = &info->dev[i];
    if (!dtperipheral(device->kind))
      continue;
    if (!device->nirq || device->nirq > DT_IRQS ||
        (device->kind != DT_DWMAC && device->nirq != 1) ||
        (device->kind == DT_DWMAC && device->nirq > 4))
      return DT_CONFLICT;
    for (irq = 0; irq < device->nirq; irq++) {
      if (device->irq[irq].reserved || device->irq[irq].parent != controller->phandle)
        return DT_CONFLICT;
      for (j = 0; j < count; j++)
        if (sources[j].number == device->irq[irq].number)
          break;
      if (j < count) {
        if (sources[j].flags != device->irq[irq].flags)
          return DT_CONFLICT;
        continue;
      }
      if (count == capacity)
        return DT_FULL;
      sources[count].number = device->irq[irq].number;
      sources[count++].flags = device->irq[irq].flags;
    }
  }
  if (count && !storage)
    return DT_BAD;
  for (i = 0; i < count; i++) {
    storage[i].number = sources[i].number;
    storage[i].flags = sources[i].flags;
  }
  for (i = 0; i < 2; i++) {
    out->base[i] = i < controller->nreg ? controller->reg[i].base : 0;
    out->bytes[i] = i < controller->nreg ? controller->reg[i].size : 0;
  }
  out->source = count ? storage : NULL;
  out->kind = controller->kind == DT_PLIC ? HIRQ_PLIC : HIRQ_GIC;
  out->regions = controller->kind == DT_GIC ? 2 : 1;
  out->count = count;
  out->limit = controller->limit;
  out->context = context;
  out->reserved = 0;
  return 0;
}
