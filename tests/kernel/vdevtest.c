#include "ktest.h"
#include "load.h"
#include <device.h>
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8  vdev_image[], vdev_image_end[];
extern const u64 vdev_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("VDEVFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("virtio MMIO device");                                                                 \
    }                                                                                              \
  } while (0)
static void
load(Handle pid, Handle memory, u32 count, const u64 offsets[8], const u64 lengths[8])
{
  struct
  {
    Boot     header;
    Resentry resources[3];
  } packet;
  u64      trace[32];
  u32      i;
  Procinfo process;
  Rtable  *table = proctable(pid);
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = 3;
  packet.header.resources_off = sizeof(Boot);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
  packet.resources[2].tag = 257;
  packet.resources[2].handle = memory;
  testload(pid, vdev_image, (u64) (vdev_image_end - vdev_image), &packet, sizeof(packet));
  REQUIRE(!procinfo(pid, &process));
  for (i = 0; i < 32; i++)
    trace[i] = 0;
  trace[1] = count;
  for (i = 0; i < count; i++) {
    trace[4 + i] = offsets[i];
    trace[23 + i] = lengths[i];
  }
  REQUIRE(!spacewrite(process.space, vdev_trace_address, trace, sizeof(trace)));
}
void
vdevtest(const Dtinfo *info)
{
  Setid      set;
  Set        account;
  Procinfo   process;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Device     window;
  Handle     pid, memory;
  u64        page, members, offsets[8], lengths[8], trace[32];
  u32 i, j, count, id, pages = 0, probes = 0, empty = 0, found = 0, negotiated = 0, resets = 0;
  u32 rng = 0, writable = 0, readonly = 0;
  u64 rwsectors = 0, rosectors = 0;
  int error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < info->ndev; i++) {
    if (info->dev[i].kind != DT_VIRTIO)
      continue;
    page = ALIGN_DOWN(info->dev[i].reg[0].base, PAGE_SIZE);
    for (j = 0; j < i; j++)
      if (info->dev[j].kind == DT_VIRTIO && ALIGN_DOWN(info->dev[j].reg[0].base, PAGE_SIZE) == page)
        break;
    if (j < i)
      continue;
    members = 0;
    count = 0;
    for (j = i; j < info->ndev; j++)
      if (info->dev[j].kind == DT_VIRTIO &&
          ALIGN_DOWN(info->dev[j].reg[0].base, PAGE_SIZE) == page) {
        REQUIRE(count < 8);
        members |= (u64) 1 << j;
        offsets[count] = info->dev[j].reg[0].base - page;
        lengths[count++] = info->dev[j].reg[0].size;
      }
    REQUIRE(count && !devgroup(page, members, &id));
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid) && !procinfo(pid, &process));
    REQUIRE(!procmmioinstallgroup(pid, page, members, 3, &memory));
    load(pid, memory, count, offsets, lengths);
    REQUIRE(!procpublish(pid) && !procrunone());
    REQUIRE(!spaceread(process.space, vdev_trace_address, trace, sizeof(trace)));
    if (trace[31]) {
      putstr("VDEVUSER fail=");
      puthex(trace[31]);
      putstr(" probes=");
      puthex(trace[12]);
      putstr("\n");
    }
    REQUIRE(trace[0] == 99 && !trace[31] && trace[12] == count && trace[13] + trace[14] == count &&
            trace[14] == trace[17] + trace[18] + trace[19] && trace[15] == trace[14] &&
            trace[16] == 2 * trace[14]);
    probes += (u32) trace[12];
    empty += (u32) trace[13];
    found += (u32) trace[14];
    negotiated += (u32) trace[15];
    resets += (u32) trace[16];
    rng += (u32) trace[17];
    writable += (u32) trace[18];
    readonly += (u32) trace[19];
    rwsectors += trace[20];
    rosectors += trace[21];
    pages++;
    REQUIRE(!lifestat(process.life, &life) && life.state == LIFE_ENDED &&
            life.reason == LIFE_EXIT && life.code == 42);
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
    REQUIRE(!devstat(id, &window) && !window.refs && !window.mappings);
  }
  REQUIRE(pages && rng == 1 && writable == 1 && readonly == 1 && probes == empty + found &&
          !devcheck());
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !groupcheck());
  putstr("VIRTIOCHECK pages=");
  puthex(pages);
  putstr(" probes=");
  puthex(probes);
  putstr(" empty=");
  puthex(empty);
  putstr(" found=");
  puthex(found);
  putstr(" negotiated=");
  puthex(negotiated);
  putstr(" resets=");
  puthex(resets);
  putstr(" rng=");
  puthex(rng);
  putstr(" rwsectors=");
  puthex(rwsectors);
  putstr(" rosectors=");
  puthex(rosectors);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
