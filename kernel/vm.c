/* Kernel-only bootstrap address space. No user mappings or runtime VM transactions. */
#include <boot.h>
#include <diag.h>
#include <page.h>
#include <vm.h>
#include <irq.h>
#include <hal/boot.h>
#include <hal/vm.h>

static Hvs kernelspace;
static u64 delta, uartoffset;
static u32 active;
static void (*continuation)(Hvs *space);

void *
kaddr(u64 pa)
{
  return (void *) (pa + (active ? delta : 0));
}

u64
kphysical(const void *va)
{
  return (u64) va - (active ? delta : 0);
}

static int
tablealloc(void *context, u64 *pa)
{
  Pgblk block;
  UNUSED(context);
  if (pgalloc(0, NULL, &block))
    return -1;
  *pa = block.pa;
  return 0;
}

static void
tablefree(void *context, u64 pa)
{
  Pgblk block;
  UNUSED(context);
  block.pa = pa;
  block.order = block.reserved = 0;
  if (pgfree(block))
    panic("page table free");
}

static void *
tableaccess(void *context, u64 pa)
{
  UNUSED(context);
  return kaddr(pa);
}

static Pgsource
source(void)
{
  Pgsource s;
  s.alloc = tablealloc;
  s.free = tablefree;
  s.access = tableaccess;
  s.context = NULL;
  return s;
}

static void
map(u64 va, u64 pa, u64 prot, u32 attr)
{
  if (hal_vm_map(&kernelspace, va, pa, prot, attr)) {
    hal_vm_destroy(&kernelspace);
    panic("kernel mapping failed");
  }
}

static u64
permissions(const Dtinfo *info, u64 pa)
{
  u32 i;
  for (i = 0; i < 2; i++)
    if (info->bootedge[i].size && pa == info->bootedge[i].base)
      return VM_READ;
  if (info->initrd.size && pa < info->initrd.base + info->initrd.size &&
      pa + PAGE_SIZE > info->initrd.base)
    return VM_READ;
  if (pa >= (u64) __text_start && pa < (u64) __text_end)
    return VM_READ | VM_EXEC;
  if (pa >= (u64) __rodata_start && pa < (u64) __rodata_end)
    return VM_READ;
  return VM_READ | VM_WRITE;
}

static int
guard(u64 pa)
{
  return pa == (u64) __stack_guard || pa == (u64) __emergency_guard;
}

static int
reserved(const Dtinfo *info, u32 firmware, u64 pa)
{
  u32 i;
  for (i = 0; i < firmware; i++) {
    const Dtrange *r = &info->reserved[i];
    if (pa < r->base + r->size && pa + PAGE_SIZE > r->base)
      return 1;
  }
  return 0;
}

static void
vmhigh(void)
{
  Hpte   result;
  u64    local;
  Pgstat pages;
  /* Recreate function pointers at their new PC-relative addresses. */
  kernelspace.source = source();
  continuation = (void (*)(Hvs *))((u64) continuation + delta);
  hal_debug_rebase(KIO + uartoffset);
  hal_vm_drop_low(&kernelspace);
  if (hal_vm_lookup(&kernelspace, kphysical(__text_start), &result) ||
      hal_vm_lookup(&kernelspace, (u64) __stack_guard, &result) ||
      hal_vm_lookup(&kernelspace, (u64) __emergency_guard, &result) || pgcheck())
    panic("low mapping/guard");
  local = (u64) &local;
  if (local < (u64) __stack_bottom || local >= (u64) __stack_top)
    panic("high stack");
  pgstat(&pages);
  if (pages.allocated != kernelspace.pages)
    panic("page table accounting");
  putstr("VM root=");
  puthex(kernelspace.root);
  putstr(" delta=");
  puthex(delta);
  putstr(" pc=");
  puthex((u64) vmhigh);
  putstr(" sp=");
  puthex(local);
  putstr(" tables=");
  puthex(kernelspace.pages);
  putstr(" uart=");
  puthex(KIO + uartoffset);
  putstr("\n");
  continuation(&kernelspace);
  hal_halt();
}

NORETURN void
vmstart(const Dtinfo *info, u32 firmware, void (*next)(Hvs *))
{
  u64      pa, end, base = ~(u64) 0, uart = 0, prot;
  u32      i, found = 0;
  Pgsource s = source();
  if (active || firmware > info->nreserved)
    panic("VM initialization state");
  for (i = 0; i < info->nram; i++)
    base = MIN(base, info->ram[i].base);
  for (i = 0; i < info->ndev; i++) {
    const Dtdev *d = &info->dev[i];
    if (d->kind == DT_UART16550 || d->kind == DT_PL011) {
      uart = d->reg[0].base;
      if ((uart & PAGE_MASK) + d->reg[0].size > PAGE_SIZE)
        panic("diagnostic UART span");
      found++;
    }
  }
  if (found != 1 || hal_vm_create(&kernelspace, &s))
    panic("kernel root");
  delta = KBASE - base;
  uartoffset = uart & PAGE_MASK;
  for (i = 0; i < info->nram; i++) {
    pa = info->ram[i].base;
    end = pa + info->ram[i].size;
    if (end - base > (1UL << 30))
      panic("direct map span");
    for (; pa < end; pa += PAGE_SIZE) {
      if (guard(pa) || reserved(info, firmware, pa))
        continue;
      prot = permissions(info, pa);
      map(pa + delta, pa, prot, VM_NORMAL);
    }
  }
  for (pa = (u64) __kernel_start; pa < (u64) __kernel_end; pa += PAGE_SIZE) {
    if (!guard(pa))
      map(pa, pa, permissions(info, pa), VM_NORMAL);
  }
  {
    Hirq       controller;
    Hirqsource sources[HIRQ_SOURCES];
    u64        windows[HIRQ_WINDOWS];
    int        count;
    if (irqdescribe(info, &controller, sources, HIRQ_SOURCES) ||
        (count = hal_irq_pages(&controller, windows)) < 0)
      panic("interrupt controller windows");
    for (i = 0; i < (u32) count; i++)
      map(KIRQ_DIST + (u64) i * PAGE_SIZE, windows[i], VM_READ | VM_WRITE, VM_DEVICE);
  }
  map(KIO, ALIGN_DOWN(uart, PAGE_SIZE), VM_READ | VM_WRITE, VM_DEVICE);
  map(ALIGN_DOWN(uart, PAGE_SIZE), ALIGN_DOWN(uart, PAGE_SIZE), VM_READ | VM_WRITE, VM_DEVICE);
  continuation = next;
  pgrelocate(delta);
  active = 1;
  hal_vm_enter(kernelspace.root, delta, vmhigh);
}
