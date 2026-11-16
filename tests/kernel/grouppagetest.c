#include "ktest.h"
#include "load.h"
#include <device.h>
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8  group_image[], group_image_end[];
extern const u64 group_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("MMIOGROUPFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("MMIO page group");                                                                    \
    }                                                                                              \
  } while (0)
static void
loadgroup(Handle pid, Handle memory, u32 mode, u32 count, const u64 offsets[8], Handle notify)
{
  struct
  {
    Boot     header;
    Resentry resources[4];
  } packet;
  u64      data[32];
  Rtable  *table = proctable(pid);
  Procinfo process;
  u32      i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = notify ? 4 : 3;
  packet.header.resources_off = sizeof(Boot);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
  packet.resources[2].tag = 257;
  packet.resources[2].handle = memory;
  if (notify) {
    packet.resources[3].tag = 258;
    packet.resources[3].handle = notify;
  }
  testload(pid, group_image, (u64) (group_image_end - group_image), &packet, sizeof(packet));
  REQUIRE(!procinfo(pid, &process));
  for (i = 0; i < 32; i++)
    data[i] = 0;
  data[1] = mode;
  data[2] = count;
  for (i = 0; i < count; i++)
    data[4 + i] = offsets[i];
  REQUIRE(!spacewrite(process.space, group_trace_address, data, sizeof(data)));
}
static void
collect(Handle pid, Procinfo *process)
{
  Life       life;
  Liferesult result;
  int        error;
  REQUIRE(!lifestat(process->life, &life) && life.state == LIFE_ENDED && life.reason == LIFE_EXIT &&
          life.code == 42);
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error && !lifecollect(process->life, 1, &result));
}
void
grouppagetest(const Dtinfo *info)
{
  u64      page = 0, members = 0, offsets[8], trace[32];
  u32      first = DT_DEVICES, count = 0, i, window_id;
  Setid    set;
  Set      account;
  Handle   owner, peer, memory, notify;
  Procinfo a, b;
  Device   window;
  Hpte     entry;
  Pgstat   before, after;
  pgstat(&before);
  for (i = 0; i < info->ndev; i++)
    if (info->dev[i].kind == DT_VIRTIO) {
      first = i;
      page = ALIGN_DOWN(info->dev[i].reg[0].base, PAGE_SIZE);
      break;
    }
  REQUIRE(first < DT_DEVICES);
  for (i = 0; i < info->ndev; i++)
    if (info->dev[i].kind == DT_VIRTIO && ALIGN_DOWN(info->dev[i].reg[0].base, PAGE_SIZE) == page) {
      REQUIRE(count < 8);
      members |= (u64) 1 << i;
      offsets[count++] = info->dev[i].reg[0].base - page;
    }
  REQUIRE(count && !devgroup(page, members, &window_id) && !devstat(window_id, &window));
  REQUIRE(window.allowed && window.pages.base == page && window.pages.size == PAGE_SIZE);
  if (count > 1)
    REQUIRE(devgroup(page, (u64) 1 << first, &i) == DEV_DENIED &&
            devwindow(first, 0, &i) == DEV_DENIED);
  REQUIRE(devgroup(page, members ^ ((u64) 1 << 63), &i) == DEV_DENIED);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &owner) && !procinfo(owner, &a));
  REQUIRE(!procmmioinstallgroup(owner, page, members, 3, &memory));
  loadgroup(owner, memory, 0, count, offsets, 0);
  REQUIRE(!procpublish(owner) && !procrunone());
  REQUIRE(!spaceread(a.space, group_trace_address, trace, sizeof(trace)));
  if (trace[31]) {
    putstr("GROUPUSER fail=");
    puthex(trace[31]);
    putstr(" version=");
    puthex(trace[13]);
    putstr(" probes=");
    puthex(trace[12]);
    putstr("\n");
  }
  REQUIRE(trace[0] == 1 && !trace[31] && trace[12] == count);
  REQUIRE(spacelookup(a.space, 0x2000000, &entry) && entry.pa == page &&
          entry.prot == (VM_READ | VM_USER) && entry.attr == VM_DEVICE);
  REQUIRE(!devstat(window_id, &window) && window.refs == 1 && window.mappings == 1);
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &peer) && !procinfo(peer, &b));
  REQUIRE(!procmmioinstallgroup(peer, page, members, 3, &memory));
  REQUIRE(!localendpoint(proctable(peer), a.identity, LOCAL_NOTIFY, LOCAL_USE, (u64) 1 << 32, 0,
                         &notify));
  loadgroup(peer, memory, 1, count, offsets, notify);
  REQUIRE(!procpublish(peer) && !procrunone());
  REQUIRE(!spaceread(b.space, group_trace_address, trace, sizeof(trace)) && trace[0] == 99 &&
          !trace[31] && trace[3]);
  REQUIRE(!devstat(window_id, &window) && window.mappings == 1);
  collect(peer, &b);
  REQUIRE(!procrunone());
  REQUIRE(!spaceread(a.space, group_trace_address, trace, sizeof(trace)) && trace[0] == 99 &&
          !trace[31]);
  collect(owner, &a);
  REQUIRE(!devstat(window_id, &window) && !window.refs && !window.mappings);
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &owner) && !procinfo(owner, &a));
  REQUIRE(!procmmioinstallgroup(owner, page, members, 3, &memory));
  loadgroup(owner, memory, 2, count, offsets, 0);
  REQUIRE(!procpublish(owner) && !procrunone());
  REQUIRE(!spaceread(a.space, group_trace_address, trace, sizeof(trace)) && trace[0] == 99 &&
          trace[12] == count && !trace[31]);
  collect(owner, &a);
  REQUIRE(!devstat(window_id, &window) && !window.refs && !window.mappings && !devcheck());
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !groupcheck());
  putstr("MMIOGROUPCHECK members=");
  puthex(count);
  putstr(" probes=");
  puthex(2 * count);
  putstr(" denied=");
  puthex(1);
  putstr(" handoff=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
