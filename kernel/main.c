/* Bootstrap initialization; explicit test images select their guest payloads. */
#include <boot.h>
#include <diag.h>
#include <dtb.h>
#include <page.h>
#include <vm.h>
#include <space.h>
#include <kstack.h>
#if BOOT_CASE != 0
#include <ktest.h>
#endif
#include <hal/boot.h>
#include <hal/clock.h>
#include <clock.h>
#include <hal/irq.h>
#include <hal/context.h>

static volatile u64 zeroed;
static volatile u64 initialized = 0x12345678;

static Dtinfo hardware;
static u32    firmware;

static void
field(const char *name, u64 number)
{
  putstr(name);
  puthex(number);
}

static void
rangeinfo(const char *name, const Dtrange *range)
{
  putstr(name);
  field(" base=", range->base);
  field(" size=", range->size);
  putstr("\n");
}

static void
physicalinit(void)
{
  u32    i;
  u64    count = 0, bytes, base = 0;
  Pgstat stats;
  for (i = 0; i < hardware.nram; i++) {
    if (hardware.ram[i].base & PAGE_MASK || hardware.ram[i].size & PAGE_MASK ||
        hardware.ram[i].size / PAGE_SIZE > (1UL << 18) - count)
      panic("RAM metadata capacity");
    count += hardware.ram[i].size / PAGE_SIZE;
  }
  bytes = ALIGN_UP(count * sizeof(Page), PAGE_SIZE);
  for (i = 0; i < hardware.nusable; i++) {
    if (hardware.usable[i].size >= bytes) {
      base = hardware.usable[i].base;
      break;
    }
  }
  if (i == hardware.nusable || dtreserve(&hardware, base, bytes) || dtusable(&hardware))
    panic("metadata reservation");
  if (pginit(&hardware, (Page *) base, (u32) count, 256))
    panic("page initialization");
  field("META base=", base);
  field(" size=", bytes);
  field(" records=", count);
  putstr("\n");
  pgstat(&stats);
  field("PAGES total=", stats.total);
  field(" managed=", stats.managed);
  field(" free=", stats.free);
  field(" allocated=", stats.allocated);
  field(" protected=", stats.protected);
  putstr("\n");
}

static int
contains(const Dtrange *range, u64 base, u64 size)
{
  return base >= range->base && base - range->base < range->size &&
         size <= range->size - (base - range->base);
}

static void
discovery(u64 dtb, u64 available)
{
  u32 i, j, uart = 0, controller = 0;
  int kernelram = 0, dtbram = 0;
  u64 start = (u64) __kernel_start, end = (u64) __kernel_end;
  {
    int error = dtread((const void *) dtb, available, &hardware);
    if (error) {
      field("DT error=", (u64) (isize) error);
      putstr("\n");
      panic("dtb parse");
    }
  }
  if (hardware.cpus != 1)
    panic("cpu profile");
  if (dtb < end && dtb + hardware.size > start)
    panic("dtb kernel overlap");
  for (i = 0; i < hardware.nram; i++) {
    if (contains(&hardware.ram[i], start, end - start))
      kernelram = 1;
    if (contains(&hardware.ram[i], dtb, hardware.size))
      dtbram = 1;
  }
  if (!kernelram || !dtbram)
    panic("boot outside RAM");
  for (i = 0; i < hardware.nreserved; i++) {
    const Dtrange *r = &hardware.reserved[i];
    if ((r->base < end && r->base + r->size > start) ||
        (r->base < dtb + hardware.size && r->base + r->size > dtb))
      panic("boot reserved overlap");
  }
  firmware = hardware.nreserved;
  if (dtreserve(&hardware, start, end - start) || dtreserve(&hardware, dtb, hardware.size) ||
      dtusable(&hardware))
    panic("boot reservation");
  physicalinit();
  field("DT ram=", hardware.nram);
  field(" reserved=", hardware.nreserved);
  field(" usable=", hardware.nusable);
  field(" devices=", hardware.ndev);
  field(" cpus=", hardware.cpus);
  field(" timebase=", hardware.timebase);
  putstr("\n");
  for (i = 0; i < hardware.nram; i++)
    rangeinfo("RAM", &hardware.ram[i]);
  for (i = 0; i < hardware.nreserved; i++)
    rangeinfo("RESERVED", &hardware.reserved[i]);
  for (i = 0; i < hardware.nusable; i++)
    rangeinfo("USABLE", &hardware.usable[i]);
  for (i = 0; i < hardware.ndev; i++) {
    const Dtdev *d = &hardware.dev[i];
    if (d->kind == DT_UART16550 || d->kind == DT_PL011)
      uart++;
    if (d->kind == DT_PLIC || d->kind == DT_GIC)
      controller++;
    field("DEVICE index=", i);
    field(" kind=", d->kind);
    field(" protected=", d->protected);
    field(" phandle=", d->phandle);
    field(" regs=", d->nreg);
    field(" irqs=", d->nirq);
    putstr("\n");
    for (j = 0; j < d->nreg; j++)
      rangeinfo("MMIO", &d->reg[j]);
    for (j = 0; j < d->nirq; j++) {
      field("IRQ parent=", d->irq[j].parent);
      field(" number=", d->irq[j].number);
      field(" flags=", d->irq[j].flags);
      putstr("\n");
    }
  }
  if (uart != 1 || controller != 1)
    panic("required devices");
}

static void
virtualmain(Hvs *space)
{
  Htimer timer;
  u32    i, j;
  timer.distributor = timer.cpu = 0;
  timer.count = timer.reserved = 0;
  for (j = 0; j < 4; j++)
    timer.irqs[j] = timer.flags[j] = 0;
  for (i = 0; i < hardware.ndev; i++) {
    const Dtdev *d = &hardware.dev[i];
    if (d->kind == DT_GIC) {
      timer.distributor = KIRQ_DIST;
      timer.cpu = KIRQ_CPU;
    }
    if (d->kind == DT_TIMER) {
      timer.count = d->nirq;
      for (j = 0; j < d->nirq; j++) {
        timer.irqs[j] = d->irq[j].number;
        timer.flags[j] = d->irq[j].flags;
      }
    }
  }
  hal_clock_init(hardware.timebase);
  if (clockinit())
    panic("clock initialization");
  hal_trap_init();
  if (hal_timer_init(&timer))
    panic("timer initialization");
  if (kstackinit(space))
    panic("kernel stack initialization");
  meminit();
  if (spaceinit(space))
    panic("user root initialization");
#if BOOT_CASE != 0
  testmain(space);
#else
  UNUSED(space);
  putstr("READY early-boot\n");
#endif
  hal_halt();
}

void
main(u64 cpu, u64 dtb, u64 mode, u64 mask, u64 vm)
{
  u64 local, ram;
  putstr("BOOT mode=");
  puthex(mode);
  putstr(" cpu=");
  puthex(cpu);
  putstr(" dtb=");
  puthex(dtb);
  putstr(" mask=");
  puthex(mask);
  putstr(" vm=");
  puthex(vm);
  putstr("\n");
  if (cpu != 0 || mask != 1 || vm != 0)
    panic("entry state");
  if (zeroed != 0 || initialized != 0x12345678)
    panic("data initialization");
  local = (u64) &local;
  if (local < (u64) __stack_bottom || local >= (u64) __stack_top)
    panic("stack");
  /* Only this bootstrap read window is platform-fixed; resources come from DTB. */
  ram = (u64) __kernel_start & ~0xfffffffUL;
  if ((dtb & 7) || dtb < ram || dtb > ram + 0x40000000 - 40)
    panic("dtb range");
  discovery(dtb, MIN(0x200000, ram + 0x40000000 - dtb));
  putstr("LAYOUT start=");
  puthex((u64) __kernel_start);
  putstr(" end=");
  puthex((u64) __kernel_end);
  putstr(" stack=");
  puthex((u64) __stack_bottom);
  putstr(" top=");
  puthex((u64) __stack_top);
  putstr(" dtb_size=");
  puthex(hardware.size);
  putstr("\n");
  vmstart(&hardware, firmware, virtualmain);
}
