#include <bootstart.h>
#include <bootstore.h>
#include <bootimage.h>
#include <proc.h>
#include <diag.h>
#include <vm.h>

typedef struct Bootpacket Bootpacket;
struct Bootpacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[3];
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

NORETURN void
bootstart(const Bootdir *directory)
{
  Bootstore       source;
  const Bootitem *item;
  const u8       *data;
  Set             budget;
  Handle          pid, creator, listener;
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
     reservations. Set/domain CONTROL and SYSTEM are separate later grants. */
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
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Bootpacket, arg);
  packet.header.resource_count = 3;
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
