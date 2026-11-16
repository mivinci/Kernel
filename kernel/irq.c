#include <irq.h>
#include <handle.h>
#include <diag.h>
typedef struct Source  Source;
typedef struct Binding Binding;
struct Source
{
  u32 number, binding;
};
struct Binding
{
  u64     generation;
  Imageid image;
  Event  *event;
  Handle  owner;
  u32     source, bit, state, next;
};
#ifndef IRQ_GENERATION_MAX
#define IRQ_GENERATION_MAX (~(u64) 0)
#endif
STATIC_ASSERT(IRQ_GENERATION_MAX > 0, irq_generation_limit);
STATIC_ASSERT(IRQS > 0 && IRQS <= 64, irq_capacity);
STATIC_ASSERT(sizeof(Binding) <= 64, irq_binding_budget);
static Source  sources[IRQS];
static Binding bindings[IRQS];
static u32     heads[PROCS], source_count, initialized;
static int
sameimage(Imageid a, Imageid b)
{
  return a.generation == b.generation && a.index == b.index && !a.reserved && !b.reserved;
}
static Binding *
find(Irqid id)
{
  Binding *binding;
  if (id.reserved || id.index >= IRQS)
    return NULL;
  binding = &bindings[id.index];
  return binding->state && binding->generation == id.generation ? binding : NULL;
}
int
irqinit(const Hirq *controller)
{
  u32 i, j;
  if (initialized)
    return -ERR_STATE;
  if (!controller || controller->count > IRQS || (controller->count && !controller->source))
    return -ERR_INVALID;
  for (i = 0; i < controller->count; i++) {
    if (!controller->source[i].number || controller->source[i].number > 1023)
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (controller->source[j].number == controller->source[i].number)
        return -ERR_INVALID;
  }
  for (i = 0; i < controller->count; i++)
    sources[i].number = controller->source[i].number;
  source_count = controller->count;
  initialized = 1;
  return 0;
}
int
irqsource(u32 number, u32 *out)
{
  u32 i;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!initialized)
    return -ERR_STATE;
  for (i = 0; i < source_count; i++)
    if (sources[i].number == number) {
      *out = i;
      return 0;
    }
  return -ERR_BAD_HANDLE;
}
int
irqnew(u32 source, Imageid id, Event *event, u32 bit, Irqid *out)
{
  Image    image;
  Binding *binding;
  u32      entry, slot, i, count = 0, exhausted = 0;
  int      error;
  if (!out)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  if (!initialized || source >= source_count)
    return -ERR_BAD_HANDLE;
  if (!event || bit < IRQ_BIT_MIN || bit > IRQ_BIT_MAX)
    return -ERR_INVALID;
  if (imagestat(id, &image))
    return -ERR_STALE;
  if (image.state != IMAGE_ACTIVE && image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  slot = handleindex(image.pid);
  if (slot >= PROCS)
    return -ERR_INVALID;
  if (sources[source].binding)
    return -ERR_BUSY;
  for (entry = heads[slot]; entry; entry = bindings[entry - 1].next) {
    binding = &bindings[entry - 1];
    if (binding->owner != image.pid || ++count > 16)
      panic("IRQ owner chain");
    if (binding->bit == bit)
      return -ERR_BUSY;
  }
  for (i = 0; i < IRQS; i++)
    if (!bindings[i].state) {
      if (bindings[i].generation >= IRQ_GENERATION_MAX)
        exhausted = 1;
      else
        break;
    }
  if (i == IRQS)
    return exhausted ? -ERR_OVERFLOW : -ERR_CAPACITY;
  error = imagehold(id, IMAGE_CLEANUP);
  if (error)
    return error;
  binding = &bindings[i];
  binding->generation++;
  binding->image.generation = id.generation;
  binding->image.index = id.index;
  binding->image.reserved = 0;
  binding->event = event;
  binding->owner = image.pid;
  binding->source = source;
  binding->bit = bit;
  binding->state = IRQ_DISABLED;
  binding->next = heads[slot];
  hal_irq_mask(sources[source].number);
  event->pending &= ~((u64) 1 << bit);
  sources[source].binding = i + 1;
  heads[slot] = i + 1;
  out->generation = binding->generation;
  out->index = i;
  return 0;
}
static void
revoke(Binding *binding)
{
  u32     index = (u32) (binding - bindings), *link, count = 0;
  Imageid image;
  if (binding->state == IRQ_REVOKED)
    return;
  if (!binding->state || !binding->event || sources[binding->source].binding != index + 1)
    panic("IRQ revoke source");
  link = &heads[handleindex(binding->owner)];
  while (*link && *link != index + 1) {
    if (++count > 16)
      panic("IRQ revoke chain");
    link = &bindings[*link - 1].next;
  }
  if (!*link)
    panic("IRQ missing owner");
  hal_irq_mask(sources[binding->source].number);
  binding->event->pending &= ~((u64) 1 << binding->bit);
  sources[binding->source].binding = 0;
  *link = binding->next;
  binding->next = 0;
  binding->event = NULL;
  binding->state = IRQ_REVOKED;
  image = binding->image;
  if (imagedrop(image, IMAGE_CLEANUP))
    panic("IRQ image release");
}
int
irqcancel(Irqid id, Imageid image)
{
  Binding *binding = find(id);
  if (!binding)
    return -ERR_STALE;
  if (!sameimage(binding->image, image))
    return -ERR_DENIED;
  if (binding->state == IRQ_REVOKED)
    return -ERR_REVOKED;
  revoke(binding);
  return 0;
}
int
irqdrop(Irqid id)
{
  Binding *binding = find(id);
  if (!binding)
    return -ERR_STALE;
  revoke(binding);
  binding->state = 0;
  binding->owner = 0;
  return 0;
}
void
irqend(Imageid image)
{
  Image    owner;
  Binding *binding;
  u32      entry, next, count = 0;
  if (!initialized)
    return;
  if (imagestat(image, &owner) || owner.state != IMAGE_ENDED)
    panic("IRQ image end");
  for (entry = heads[handleindex(owner.pid)]; entry; entry = next) {
    binding = &bindings[entry - 1];
    next = binding->next;
    if (++count > 16 || binding->owner != owner.pid)
      panic("IRQ ended chain");
    if (sameimage(binding->image, image))
      revoke(binding);
  }
}
int
irqack(Irqid id, Imageid image)
{
  Binding *binding = find(id);
  Image    owner;
  if (!binding)
    return -ERR_STALE;
  if (!sameimage(binding->image, image))
    return -ERR_DENIED;
  if (binding->state == IRQ_REVOKED)
    return -ERR_REVOKED;
  if (imagestat(image, &owner) || owner.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  if (binding->state == IRQ_ARMED || (binding->event->pending & ((u64) 1 << binding->bit)))
    return -ERR_STATE;
  binding->state = IRQ_ARMED;
  hal_irq_unmask(sources[binding->source].number);
  return 0;
}
int
irqfire(u64 token, Imageid *out)
{
  Binding *binding;
  Image    owner;
  u32      number = hal_irq_number(token), source;
  if (!out || !token)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  if (irqsource(number, &source)) {
    hal_irq_complete(token);
    return -ERR_BAD_HANDLE;
  }
  hal_irq_mask(number);
  hal_irq_complete(token);
  if (!sources[source].binding)
    return 0;
  binding = &bindings[sources[source].binding - 1];
  if (binding->state != IRQ_ARMED)
    return 0;
  if (imagestat(binding->image, &owner) || owner.state != IMAGE_ACTIVE ||
      owner.pid != binding->owner)
    panic("IRQ delivery identity");
  binding->state = IRQ_DELIVERED;
  eventpost(binding->event, (u64) 1 << binding->bit);
  out->generation = binding->image.generation;
  out->index = binding->image.index;
  return 1;
}
int
irqwaiting(void)
{
  u32 i;
  for (i = 0; i < IRQS; i++)
    if (bindings[i].state == IRQ_ARMED)
      return 1;
  return 0;
}
int
irqstat(Irqid id, Irqstat *out)
{
  Binding *binding = find(id);
  if (!out)
    return -ERR_INVALID;
  if (!binding)
    return -ERR_STALE;
  out->image = binding->image;
  out->owner = binding->owner;
  out->number = sources[binding->source].number;
  out->bit = binding->bit;
  out->state = binding->state;
  out->reserved = 0;
  return 0;
}
int
irqcheck(void)
{
  Binding *binding;
  Image    image;
  u32      i, j, entry, count, seen[IRQS];
  for (i = 0; i < IRQS; i++)
    seen[i] = 0;
  for (i = 0; i < PROCS; i++) {
    count = 0;
    for (entry = heads[i]; entry; entry = binding->next) {
      if (entry > IRQS || ++count > 16)
        return -ERR_STATE;
      binding = &bindings[entry - 1];
      if (seen[entry - 1]++ || binding->state < IRQ_DISABLED || binding->state > IRQ_DELIVERED ||
          handleindex(binding->owner) != i)
        return -ERR_STATE;
    }
  }
  for (i = 0; i < IRQS; i++) {
    binding = &bindings[i];
    if (!binding->state)
      continue;
    if (!binding->generation || binding->source >= source_count || binding->bit < IRQ_BIT_MIN ||
        binding->bit > IRQ_BIT_MAX)
      return -ERR_STATE;
    if (binding->state == IRQ_REVOKED) {
      if (seen[i] || binding->event || binding->next)
        return -ERR_STATE;
      continue;
    }
    if (seen[i] != 1 || sources[binding->source].binding != i + 1 || !binding->event ||
        imagestat(binding->image, &image) || image.state == IMAGE_ENDED ||
        image.pid != binding->owner)
      return -ERR_STATE;
    for (j = i + 1; j < IRQS; j++)
      if (bindings[j].state && bindings[j].state != IRQ_REVOKED &&
          bindings[j].owner == binding->owner && bindings[j].bit == binding->bit)
        return -ERR_STATE;
  }
  for (i = 0; i < source_count; i++)
    if (sources[i].binding &&
        (sources[i].binding > IRQS || bindings[sources[i].binding - 1].source != i ||
         bindings[sources[i].binding - 1].state < IRQ_DISABLED ||
         bindings[sources[i].binding - 1].state > IRQ_DELIVERED))
      return -ERR_STATE;
  return 0;
}
