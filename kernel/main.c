/* Bootstrap initialization; explicit test images select their guest payloads. */
#include <boot.h>
#include <diag.h>
#include <dtb.h>
#include <page.h>
#include <vm.h>
#include <space.h>
#include <kstack.h>
#include <bootpkg.h>
#include <bootstore.h>
#include <bootstart.h>
#include <bootcpu.h>
#if BOOT_CASE != 0
#include <ktest.h>
#endif
#include <hal/boot.h>
#include <hal/clock.h>
#include <clock.h>
#include <hal/irq.h>
#include <irq.h>
#include <device.h>
#include <hal/context.h>

static volatile u64 zeroed;
static volatile u64 initialized = 0x12345678;

static Dtinfo         hardware;
static u32            firmware;
static Bootdir        package;
static Dtuart         diagnostic;
static Bootcpus       cpustate;
static volatile isize cpuerror;

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
  u32     count;
  Dtrange metadata;
  Pgstat  stats;
  if (pgplan(&hardware, &metadata, &count))
    panic("RAM metadata plan");
  if (dtreserve(&hardware, metadata.base, metadata.size) || dtusable(&hardware))
    panic("metadata reservation");
  if (pginit(&hardware, (Page *) metadata.base, count, 256))
    panic("page initialization");
  field("META base=", metadata.base);
  field(" size=", metadata.size);
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
  u64 pkgbase = 0, pkgend = 0;
  {
    int error = dtread((const void *) dtb, available, &hardware);
    if (error) {
      field("DT error=", (u64) (isize) error);
      putstr("\n");
      panic("dtb parse");
    }
  }
  if (hardware.cpus != cpustate.count)
    panic("cpu descriptor changed");
  for (i = 0; i < hardware.cpus; i++)
    if (hardware.cpu[i].id != cpustate.cpu[i].id || hardware.cpu[i].method != cpustate.method ||
        hardware.cpu[i].intc != cpustate.cpu[i].intc)
      panic("cpu identity changed");
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
  if (dtinitrdcheck(&hardware, start, end - start, dtb))
    panic("initrd boot ownership");
  if (hardware.initrd.size) {
    u64 window = start & ~0xfffffffUL;
    pkgbase = ALIGN_DOWN(hardware.initrd.base, PAGE_SIZE);
    pkgend = ALIGN_UP(hardware.initrd.base + hardware.initrd.size, PAGE_SIZE);
    if (pkgbase < window || pkgend > window + KMAP_BYTES)
      panic("initrd read window");
  }
  for (i = 0; i < hardware.nreserved; i++) {
    const Dtrange *r = &hardware.reserved[i];
    if ((r->base < end && r->base + r->size > start) ||
        (r->base < ALIGN_UP(dtb + hardware.size, PAGE_SIZE) &&
         r->base + r->size > ALIGN_DOWN(dtb, PAGE_SIZE)))
      panic("boot reserved overlap");
  }
  firmware = hardware.nreserved;
  if (hardware.initrd.size) {
    int error = bootpkgcheck((const void *) hardware.initrd.base, hardware.initrd.size,
                             hal_boot_machine(), &package);
    if (error) {
      field("BOOTPKG error=", (u64) (isize) error);
      putstr("\n");
      panic("boot package");
    }
    if (dtreserve(&hardware, hardware.initrd.base, hardware.initrd.size))
      panic("initrd reservation");
    rangeinfo("INITRD", &hardware.initrd);
    field("BOOTPKG count=", package.header.count);
    field(" init=", package.header.init_index);
    field(" checksum=", package.header.checksum);
    putstr("\n");
  }
#if BOOT_CASE == 0 || BOOT_CASE == 53
  else
    panic("initrd missing");
#endif
  if (dtreserve(&hardware, start, end - start) || dtreserve(&hardware, dtb, hardware.size) ||
      dtusable(&hardware))
    panic("boot reservation");
  if (bootstoreprepare(&hardware))
    panic("boot package retention");
  physicalinit();
  if (hardware.initrd.size) {
    Bootstore retained;
    bootstorestat(&retained);
    field("BOOTSTORE pages=", retained.pages);
    field(" copies=", retained.copies);
    field(" retained=", retained.retained);
    field(" refs=", retained.refs);
    putstr("\n");
    for (i = 0; i < 2; i++)
      if (hardware.bootedge[i].size)
        rangeinfo("BOOTEDGE", &hardware.bootedge[i]);
  }
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
    field(" mappable=", d->mappable);
    field(" groupable=", d->groupable);
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
  Htimer     timer;
  Hirq       controller;
  Hirqsource sources[HIRQ_SOURCES];
  const u64  windows[HIRQ_WINDOWS] = {KIRQ_DIST, KIRQ_CPU, KIRQ_ENABLE};
  u32        i, j;
  timer.count = timer.reserved = 0;
  for (j = 0; j < 4; j++)
    timer.irqs[j] = timer.flags[j] = 0;
  for (i = 0; i < hardware.ndev; i++) {
    const Dtdev *d = &hardware.dev[i];
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
  if (irqdescribe(&hardware, cpustate.current, &controller, sources, HIRQ_SOURCES))
    panic("interrupt controller description");
  field("IRQ_CPU current=", cpustate.current);
  field(" context=", controller.context);
  field(" regions=", controller.regions);
  putstr("\n");
  if (hal_irq_init(&controller, windows) || irqinit(&controller))
    panic("interrupt controller initialization");
  if (hal_timer_init(&timer))
    panic("timer initialization");
  if (kstackinit(space))
    panic("kernel stack initialization");
  if (devinit(&hardware))
    panic("device window inventory");
  meminit();
  if (spaceinit(space))
    panic("user root initialization");
#if BOOT_CASE != 0
  testmain(space, &hardware, cpustate.current);
#else
  bootstart(&package);
#endif
  hal_halt();
}

void
main(u64 cpu, u64 dtb, u64 mode, u64 mask, u64 vm)
{
  u64 local, ram;
  u32 i;
  /* No UART access before the bounded firmware descriptor has been checked. */
  if (mask != 1 || vm != 0)
    hal_halt();
  ram = (u64) __kernel_start & ~0xfffffffUL;
  if ((dtb & 7) || dtb < ram || dtb > ram + KMAP_BYTES - 40 ||
      dtconsole((const void *) dtb, MIN(0x200000, ram + KMAP_BYTES - dtb), &hardware, &diagnostic))
    hal_halt();
  cpuerror = bootcpucheck(&hardware, cpu, &cpustate);
  if (cpuerror || hal_debug_init(&diagnostic))
    hal_halt();
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
  field("CPU_BOOT current=", cpustate.current);
  field(" count=", cpustate.count);
  field(" off=", cpustate.count - 1);
  field(" method=", cpustate.method);
  putstr("\n");
  for (i = 0; i < cpustate.count; i++) {
    field("CPU_STATE id=", cpustate.cpu[i].id);
    field(" state=", cpustate.cpu[i].state);
    field(" intc=", cpustate.cpu[i].intc);
    putstr("\n");
  }
  if (zeroed != 0 || initialized != 0x12345678)
    panic("data initialization");
  local = (u64) &local;
  if (local < (u64) __stack_bottom || local >= (u64) __stack_top)
    panic("stack");
  discovery(dtb, MIN(0x200000, ram + KMAP_BYTES - dtb));
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
  vmstart(&hardware, firmware, cpustate.current, virtualmain);
}
