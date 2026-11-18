#include <bootstart.h>
#include <abi/observe.h>
#include <bootstore.h>
#include <bootimage.h>
#include <proc.h>
#include <diag.h>
#include <vm.h>
#include <bootresource.h>
#include <device.h>
#include <prep.h>

typedef struct Bootpacket Bootpacket;
struct Bootpacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[4];
  u8       name[BOOTPKG_NAME_BYTES + 1];
};
static Bootpacket packet;
static Setid      sets[AC_SETS];
STATIC_ASSERT(AC_SETS == 16, initial_set_policy_count);

static void
field(const char *name, u64 value)
{
  putstr(name);
  puthex(value);
}
static Handle
resource(Rtable *table, u32 subtype)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == subtype)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  panic("initial resource");
}

static int
initialdevices(Handle pid, Rtable *table, Imageid image, const Dtinfo *hardware, const Dtuart *uart,
               Handle *system)
{
  Handle memory = 0, irq = 0;
  u32    i, device = DT_DEVICES, matches = 0, window, number = 0, usable = 0;
  int    error;
  if (!hardware || hardware->ndev > DT_DEVICES || !uart || !system)
    return -ERR_INVALID;
  *system = 0;
  for (i = 0; i < hardware->ndev; i++)
    if (hardware->dev[i].kind == uart->kind && hardware->dev[i].nreg == 1 &&
        hardware->dev[i].reg[0].base == uart->reg.base &&
        hardware->dev[i].reg[0].size == uart->reg.size) {
      device = i;
      matches++;
    }
  if (matches != 1)
    return -ERR_INVALID;
  if (uart->shift <= 4 && (uart->width == 1 || uart->width == 4) &&
      !(uart->reg.base & (uart->width - 1)) &&
      ((uart->kind == BOOT_UART_NS16550 && !((1U << uart->shift) & (uart->width - 1))) ||
       (uart->kind == BOOT_UART_PL011 && !uart->shift && uart->width == 4))) {
    error = devwindow(device, 0, &window);
    if (!error)
      usable = 1;
    else if (error != DEV_DENIED)
      return -ERR_INVALID;
  }
  error = localrooms(table, usable ? 3 : 1);
  if (error)
    return error;
  if (usable) {
    if (hardware->dev[device].nirq != 1)
      return -ERR_NOTSUP;
    number = hardware->dev[device].irq[0].number;
    error = procmmioinstall(pid, device, 0, VM_READ | VM_WRITE | LOCAL_GRANT, &memory);
    if (!error)
      error = localirqsource(table, number, &irq);
    if (error)
      goto failed;
  }
  error = localsystem(table, image, system);
  if (!error)
    error = bootresourceinit(table, image, usable ? uart : NULL, memory, irq, number);
  if (!error)
    return 0;
failed:
  if (*system && localfree(table, *system))
    panic("initial SYSTEM rollback");
  if (irq && localfree(table, irq))
    panic("initial IRQ source rollback");
  if (memory && localfree(table, memory))
    panic("initial MMIO rollback");
  *system = 0;
  return error;
}
NORETURN void
bootstart(const Bootdir *directory, const Dtinfo *hardware, const Dtuart *uart)
{
  Bootstore       source;
  const Bootitem *item;
  const u8       *data;
  Set             budget;
  Handle          pid, creator, listener, system, observer;
  Createpolicy    policy;
  Procinfo        process;
  Rtable         *table;
  Kstack          stack;
  Space           space;
  u32             i, announced = 0;
  int             error;
  if (!directory || bootstorestat(&source) || !source.bytes ||
      directory->header.bytes != source.bytes || !directory->header.count ||
      directory->header.count > BOOTPKG_ITEMS ||
      directory->header.init_index >= directory->header.count)
    panic("initial package source");
  item = &directory->item[directory->header.init_index];
  data = kaddr(source.base);
  /* Fixed flat cohorts, not runtime Set creation. Index 0 is init, 1 ordinary
     applications; 2..15 are service cohorts. Limits are ceilings, not RAM
     reservations. Set/domain CONTROL remain separate later grants. */
  for (i = 0; i < AC_SETS; i++)
    if (setcreate(i == 1 ? 32768 : 4096, i == 1 ? 40960 : 6144,
                  i == 0   ? 1
                  : i == 1 ? 48
                           : 8,
                  &sets[i]))
      panic("initial Set policy");
  if (procboot(sets[0], 4096, 6144, LOCALS, &pid) || pid != 1 || procinfo(pid, &process))
    panic("initial process admission");
  table = proctable(pid);
  if (!table || spacebootmap(process.space))
    panic("initial package view");
  policy.memlimit = 4096;
  policy.exec_limit = 6144;
  policy.resources = LOCALS;
  policy.pending = 16;
  policy.rights = CREATE_USE | CREATE_CREDENTIAL | CREATE_SERVICE | LOCAL_GRANT;
  for (i = 0; i < AC_SETS; i++) {
    policy.set = sets[i];
    if (localcreate(table, &policy, &creator))
      panic("initial CREATE policy");
    if (i == 2) {
      packet.resources[2].tag = BOOT_CREATE;
      packet.resources[2].handle = creator;
    }
  }
  if (localendpoint(table, process.identity, LOCAL_CALL, LOCAL_USE | LOCAL_GRANT, 0, 0, &listener))
    panic("initial listener");
  if (initialdevices(pid, table, process.identity, hardware, uart, &system))
    panic("initial device authority");
  if (localobserve(table, OBSERVE_MONITOR, &observer))
    panic("initial monitor authority");
  packet.resources[3].tag = BOOT_SYSTEM;
  packet.resources[3].handle = system;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Bootpacket, arg);
  packet.header.resource_count = 4;
  packet.header.resources_off = offsetof(Bootpacket, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.header.package_va = BOOT_PACKAGE_LO + (source.base & PAGE_MASK);
  packet.header.package_bytes = source.bytes;
  packet.arg.offset = offsetof(Bootpacket, name);
  packet.arg.length = (u32) item->name_bytes + 1;
  for (i = 0; i < item->name_bytes; i++)
    packet.name[i] = data[item->name_off + i];
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = resource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = resource(table, LOCAL_ALLOC);
  if (bootimageload(pid, data + item->load_off, item->load_bytes, &packet, sizeof(packet)) ||
      spacestat(process.space, &space) || kstackstat(process.stack, &stack) ||
      setstat(sets[0], &budget) || bootstorestat(&source))
    panic("initial image load");
  field("INITPOLICY sets=", AC_SETS);
  field(" creates=", AC_SETS);
  field(" listener=", (u64) listener);
  field(" default_set=", settag(sets[2]));
  putstr("\n");
  field("INITPACKAGE pages=", source.pages);
  field(" copies=", source.copies);
  field(" retained=", source.retained);
  field(" refs=", source.refs);
  putstr("\n");
  field("INITRETAIN charged=", budget.charged);
  field(" acct=", accttag(process.acct));
  field(" root=", space.root);
  field(" stack_table=", stack.table);
  field(" stack_va=", KSTACK_BASE + (u64) process.stack.index * KSTACK_STRIDE + PAGE_SIZE);
  putstr("\n");
  for (i = 0; i < KSTACK_PAGES; i++) {
    field("INITSTACK index=", i);
    field(" pa=", stack.pa[i]);
    putstr("\n");
  }
  if (proctimerstart() || procpublish(pid))
    panic("initial publication");
  for (;;) {
    /* Ready user work must not starve retirement or expired preparations.
       CPU root/stack, no current process; each scan has its existing bound. */
    if (procwork(1) < 0 || prepwork(1) < 0)
      panic("initial cleanup");
    error = procrunone();
    if (error == -ERR_WOULD_BLOCK) {
      if (procidle() < 0)
        panic("initial idle");
    } else if (error)
      panic("initial dispatch");
    if (!announced && !procinfo(pid, &process) && process.state == PROC_BLOCKED) {
      field("INIT pid=", (u64) pid);
      field(" image=", process.image);
      field(" state=", process.state);
      putstr("\n");
      announced = 1;
    }
  }
}
