#include <kstack.h>
#include <diag.h>
#include <vm.h>
STATIC_ASSERT((KSTACK_BASE >> 30) == (KIO >> 30), stack_parent);
STATIC_ASSERT(KSTACK_BASE + KSTACKS * KSTACK_STRIDE <= KIO + (1UL << 30), stack_region);
STATIC_ASSERT(!(KSTACK_BASE & (KSTACK_STRIDE - 1)) && KSTACK_STRIDE == (1UL << 21), stack_slot);

typedef struct Stacksource Stacksource;
struct Stacksource
{
  Kstack *stack;
  int     error;
};
static Kstack stacks[KSTACKS];
static Hvs   *kernelspace;

static Kstack *
find(Kstackid id)
{
  Kstack *s;
  if (!kernelspace || id.reserved || id.index >= KSTACKS)
    return NULL;
  s = &stacks[id.index];
  return s->state && s->generation == id.generation ? s : NULL;
}
static u64
base(const Kstack *s)
{
  return KSTACK_BASE + (u64) (s - stacks) * KSTACK_STRIDE + PAGE_SIZE;
}
static int
onstack(const Kstack *s)
{
  u64 local = (u64) &local;
  return local >= base(s) && local < base(s) + (u64) KSTACK_PAGES * PAGE_SIZE;
}
static void
freepage(Kstack *s, u64 pa)
{
  Acctblk block;
  block.acct = s->acct;
  block.block.pa = pa;
  block.block.order = block.block.reserved = 0;
  if (acctfree(block))
    panic("kernel stack page ownership");
}
static int
allocate(void *context, u64 *pa)
{
  Stacksource *source = context;
  Acctblk      block;
  if (source->stack->table)
    panic("kernel stack table boundary");
  source->error = acctalloc(source->stack->acct, 0, NULL, &block);
  if (source->error)
    return -1;
  *pa = source->stack->table = block.block.pa;
  return 0;
}
static void
release(void *context, u64 pa)
{
  Stacksource *source = context;
  if (pa != source->stack->table)
    panic("kernel stack table ownership");
  freepage(source->stack, pa);
  source->stack->table = 0;
}
static void *
access(void *context, u64 pa)
{
  UNUSED(context);
  return kaddr(pa);
}
static void
view(Kstack *s, Hvs *vs, Stacksource *source)
{
  vs->root = kernelspace->root;
  vs->pages = kernelspace->pages;
  vs->user = 0;
  source->stack = s;
  source->error = 0;
  vs->source.alloc = allocate;
  vs->source.free = release;
  vs->source.access = access;
  vs->source.context = source;
}
int
kstackinit(Hvs *kernel)
{
  Hpte entry;
  u32  i;
  if (!kernel || !kernel->pages || kernel->user || !kernel->source.access ||
      !hal_vm_lookup(kernel, KIO, &entry) || entry.attr != VM_DEVICE ||
      entry.prot != (VM_READ | VM_WRITE))
    return KS_BAD;
  for (i = 0; i < KSTACKS; i++)
    if (stacks[i].state)
      return KS_BUSY;
  kernelspace = kernel;
  return 0;
}
int
kstackcreate(Acctid acct, Kstackid *out)
{
  Kstack      *s;
  Stacksource  source;
  Acctblk      block;
  Hvs          vs;
  Hpte         entry;
  u32          slot, i, j, pages = 0, made = 0;
  u64          va;
  volatile u8 *memory;
  int          error;
  if (!kernelspace || !out)
    return KS_BAD;
  for (slot = 0; slot < KSTACKS; slot++)
    if (!stacks[slot].state && stacks[slot].generation != ~(u64) 0)
      break;
  if (slot == KSTACKS)
    return KS_FULL;
  s = &stacks[slot];
  va = base(s);
  /* Reserve the whole private leaf-table window, including both guards. */
  for (i = 0; i < KSTACK_STRIDE / PAGE_SIZE; i++)
    if (hal_vm_lookup(kernelspace, va - PAGE_SIZE + (u64) i * PAGE_SIZE, &entry))
      return KS_BUSY;
  error = accthold(acct);
  if (error)
    return error;
  s->acct = acct;
  s->table = 0;
  for (i = 0; i < KSTACK_PAGES; i++) {
    error = acctalloc(acct, 0, NULL, &block);
    if (error)
      break;
    s->pa[pages++] = block.block.pa;
    memory = kaddr(block.block.pa);
    for (j = 0; j < PAGE_SIZE; j++)
      memory[j] = 0xa5;
  }
  if (!error) {
    view(s, &vs, &source);
    for (i = 0; i < KSTACK_PAGES; i++) {
      error =
          (int) hal_vm_map(&vs, va + (u64) i * PAGE_SIZE, s->pa[i], VM_READ | VM_WRITE, VM_NORMAL);
      if (error) {
        error = source.error ? source.error : KS_BAD;
        break;
      }
      made++;
    }
    if (error) {
      while (made) {
        made--;
        if (hal_vm_unmap(&vs, va + (u64) made * PAGE_SIZE))
          panic("kernel stack rollback");
      }
    } else
      hal_vm_commit(&vs);
    kernelspace->pages = vs.pages;
  }
  if (error) {
    while (pages)
      freepage(s, s->pa[--pages]);
    if (s->table || acctdrop(acct))
      panic("kernel stack rollback accounting");
    return error;
  }
  s->generation++;
  s->refs = s->state = 1;
  out->generation = s->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}
int
kstackhold(Kstackid id)
{
  Kstack *s = find(id);
  if (!s || s->refs == 0xffffffffU)
    return KS_BAD;
  s->refs++;
  return 0;
}
int
kstackdrop(Kstackid id)
{
  Kstack *s = find(id);
  if (!s || s->refs <= 1)
    return KS_BAD;
  if (onstack(s))
    return KS_BUSY;
  s->refs--;
  return 0;
}
int
kstackfree(Kstackid id)
{
  Kstack     *s = find(id);
  Stacksource source;
  Hvs         vs;
  Hpte        entry;
  u32         i;
  if (!s)
    return KS_BAD;
  if (s->refs != 1 || onstack(s))
    return KS_BUSY;
  /* Foreign leaves would keep our charged table alive after unmap. */
  for (i = 0; i < KSTACK_STRIDE / PAGE_SIZE; i++)
    if ((i == 0 || i > KSTACK_PAGES) &&
        hal_vm_lookup(kernelspace, base(s) - PAGE_SIZE + (u64) i * PAGE_SIZE, &entry))
      return KS_BAD;
  /* Validate every page before removing the first one. */
  for (i = 0; i < KSTACK_PAGES; i++)
    if (!hal_vm_lookup(kernelspace, base(s) + (u64) i * PAGE_SIZE, &entry) ||
        entry.pa != s->pa[i] || entry.prot != (VM_READ | VM_WRITE) || entry.attr != VM_NORMAL)
      return KS_BAD;
  view(s, &vs, &source);
  for (i = 0; i < KSTACK_PAGES; i++)
    if (hal_vm_unmap(&vs, base(s) + (u64) i * PAGE_SIZE))
      panic("kernel stack unmap");
  kernelspace->pages = vs.pages;
  if (s->table)
    panic("kernel stack table retained");
  for (i = 0; i < KSTACK_PAGES; i++)
    freepage(s, s->pa[i]);
  if (acctdrop(s->acct))
    panic("kernel stack account reference");
  s->refs = s->state = 0;
  return 0;
}
int
kstackstat(Kstackid id, Kstack *out)
{
  Kstack *s = find(id);
  u32     i;
  if (!s || !out)
    return KS_BAD;
  out->generation = s->generation;
  out->acct = s->acct;
  for (i = 0; i < KSTACK_PAGES; i++)
    out->pa[i] = s->pa[i];
  out->table = s->table;
  out->refs = s->refs;
  out->state = s->state;
  return 0;
}
int
kstacktop(Kstackid id, u64 *out)
{
  Kstack *s = find(id);
  if (!s || !out)
    return KS_BAD;
  *out = base(s) + (u64) KSTACK_PAGES * PAGE_SIZE;
  return 0;
}
int
kstackused(Kstackid id, u64 *out)
{
  Kstack            *s = find(id);
  const volatile u8 *memory;
  u32                i, j;
  if (!s || !out)
    return KS_BAD;
  if (onstack(s))
    return KS_BUSY;
  for (i = 0; i < KSTACK_PAGES; i++) {
    memory = kaddr(s->pa[i]);
    for (j = 0; j < PAGE_SIZE; j++)
      if (memory[j] != 0xa5) {
        *out = (u64) (KSTACK_PAGES - i) * PAGE_SIZE - j;
        return 0;
      }
  }
  *out = 0;
  return 0;
}
