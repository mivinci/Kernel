#include "ktest.h"
#include "load.h"
#include "virtiouser.h"
#include <device.h>
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
#include <irq.h>
#include <clock.h>
#include <hal/dma.h>
#include <dma.h>
#include <vm.h>
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
load(Handle pid, Handle memory, u32 count, const u64 offsets[8], const u64 lengths[8],
     const u32 irqs[8], Memid dma[VTEST_PAGES])
{
  struct
  {
    Boot     header;
    Resentry resources[11];
  } packet;
  u64      trace[VTEST_TRACE];
  u32      i;
  Procinfo process;
  Rtable  *table = proctable(pid);
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = 3 + count;
  packet.header.resources_off = sizeof(Boot);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
  packet.resources[2].tag = 257;
  packet.resources[2].handle = memory;
  for (i = 0; i < count; i++) {
    packet.resources[3 + i].tag = VTEST_IRQ_TAG + i;
    REQUIRE(!procirqinstall(pid, irqs[i], IRQ_BIT_MIN + i, &packet.resources[3 + i].handle));
  }
  testload(pid, vdev_image, (u64) (vdev_image_end - vdev_image), &packet, sizeof(packet));
  REQUIRE(!procinfo(pid, &process));
  for (i = 0; i < VTEST_TRACE; i++)
    trace[i] = 0;
  trace[1] = count;
  for (i = 0; i < count; i++) {
    trace[4 + i] = offsets[i];
    trace[23 + i] = lengths[i];
  }
  /* QEMU virt uses identity DMA and coherent RAM. Only this trusted test
     loader passes these addresses; retain the Mem managers until the guest
     proves reset, even if its process exits on a failed transfer. */
  for (i = 0; i < VTEST_PAGES; i++) {
    REQUIRE(!memcreate(process.acct, PAGE_SIZE, &dma[i]));
    REQUIRE(!spacemap(process.space, dma[i], 0, VTEST_DMA_BASE + i * PAGE_SIZE, PAGE_SIZE,
                      VM_READ | VM_WRITE));
    REQUIRE(!mempage(dma[i], 0, &trace[32 + i]));
    REQUIRE(!hal_dma_sync(kaddr(trace[32 + i]), PAGE_SIZE, HDMA_BEFORE, HDMA_DEVICE_READ, 1));
  }
  REQUIRE(!spacewrite(process.space, vdev_trace_address, trace, sizeof(trace)));
}
static void
testpins(Setid set, u32 device)
{
  Dmaid  domain, other;
  Acctid payer, next;
  Memid  page, oldest;
  Mem    held;
  Set    account;
  u32    i;
  int    error;
  REQUIRE(!acctcreate(set, DMA_MAX_PAGES, DMA_MAX_PAGES, &payer));
  REQUIRE(!acctcreate(set, 2, 2, &next));
  REQUIRE(!dmaopen(device, payer, &domain) && dmaopen(device, next, &other) == -ERR_BUSY);
  for (i = 0; i < DMA_MAX_PAGES; i++) {
    REQUIRE(!dmaallocpage(domain, &page) && !memfree(page));
    if (!i)
      oldest = page;
  }
  REQUIRE(dmaallocpage(domain, &page) == -ERR_LIMIT && !acctclose(payer) && !acctunbind(payer));
  REQUIRE(!memstat(oldest, &held) && held.kind == MEM_DMA && held.refs == 1 && !held.managers &&
          !setstat(set, &account) && account.charged == DMA_MAX_PAGES);
  /* No DMA is submitted in this census. The real I/O cases below separately
     verify device reset before releasing their test buffers. */
  REQUIRE(dmaend(domain) == 1 && dmaallocpage(domain, &page) == -ERR_STALE &&
          dmaopen(device, next, &other) == -ERR_BUSY && !memstat(oldest, &held));
  do {
    error = dmaend(domain);
  } while (error > 0);
  REQUIRE(!error && memstat(oldest, &held) == MEM_BAD && !setstat(set, &account) &&
          !account.charged);
  REQUIRE(!dmaopen(device, next, &other) && other.generation > domain.generation &&
          dmaend(domain) == -ERR_STALE && dmaallocpage(domain, &page) == -ERR_STALE);
  REQUIRE(!dmaallocpage(other, &page) && !dmaend(other) && !memfree(page));
  REQUIRE(!acctclose(next) && !acctunbind(next));
  putstr("DMAPINCHECK pages=");
  puthex(DMA_MAX_PAGES);
  putstr("\n");
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
  Memid      dma[VTEST_PAGES];
  u64        page, members, offsets[8], lengths[8], trace[VTEST_TRACE];
  u64        started, current;
  u32        irqs[8], interrupts = 0, retired = 0;
  u32 i, j, count, id, pages = 0, probes = 0, empty = 0, found = 0, negotiated = 0, resets = 0;
  u32 rng = 0, writable = 0, readonly = 0, reads = 0, writes = 0, flushes = 0;
  u64 rwsectors = 0, rosectors = 0;
  int error;
  pgstat(&before);
  REQUIRE(!proctimerstart());
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < info->ndev; i++) {
    if (info->dev[i].kind != DT_VIRTIO)
      continue;
    if (!pages)
      testpins(set, i);
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
        lengths[count] = info->dev[j].reg[0].size;
        REQUIRE(info->dev[j].nirq == 1);
        irqs[count++] = info->dev[j].irq[0].number;
      }
    REQUIRE(count && !devgroup(page, members, &id));
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid) && !procinfo(pid, &process));
    REQUIRE(!procmmioinstallgroup(pid, page, members, 3, &memory));
    load(pid, memory, count, offsets, lengths, irqs, dma);
    REQUIRE(!procpublish(pid) && !clockmono(&started));
    do {
      error = procrunone();
      REQUIRE((!error || error == -ERR_WOULD_BLOCK) && !lifestat(process.life, &life));
      REQUIRE(!clockmono(&current) && current - started < UINT64_C(4000000000));
      if (error == -ERR_WOULD_BLOCK)
        REQUIRE(procidle() >= 0);
    } while (life.state != LIFE_ENDED);
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
            trace[16] == 2 * trace[14] + trace[18] + trace[19] &&
            trace[22] == 3 * trace[18] + 2 * trace[19] && trace[2] == trace[18] &&
            trace[3] == trace[18] && trace[38] == trace[2] + trace[3] + trace[22] &&
            trace[39] == trace[18] + trace[19]);
    interrupts += (u32) trace[38];
    retired += (u32) trace[39];
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
    reads += (u32) trace[22];
    writes += (u32) trace[2];
    flushes += (u32) trace[3];
    pages++;
    REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
    for (j = 0; j < VTEST_PAGES; j++) {
      void *address = kaddr(trace[32 + j]);
      u64   grain = hal_dma_granule();
      REQUIRE(hal_dma_sync(address, 0, HDMA_AFTER, HDMA_DEVICE_WRITE, 1) == -ERR_INVALID);
      if (grain) {
        REQUIRE(grain <= PAGE_SIZE && !(PAGE_SIZE % grain));
        REQUIRE(hal_dma_sync(address, grain - 1, HDMA_AFTER, HDMA_DEVICE_WRITE, 0) == -ERR_INVALID);
        REQUIRE(!hal_dma_sync(address, PAGE_SIZE, HDMA_BEFORE, HDMA_DEVICE_WRITE, 0));
        REQUIRE(!hal_dma_sync(address, PAGE_SIZE, HDMA_AFTER, HDMA_DEVICE_WRITE, 0));
      } else
        REQUIRE(hal_dma_sync(address, PAGE_SIZE, HDMA_AFTER, HDMA_DEVICE_WRITE, 0) == -ERR_NOTSUP);
      REQUIRE(!hal_dma_sync(address, PAGE_SIZE, HDMA_AFTER, HDMA_DEVICE_WRITE, 1));
      REQUIRE(!memfree(dma[j]));
    }
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
          !acctcheck() && !lifecheck() && !groupcheck() && !irqcheck());
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
  putstr(" reads=");
  puthex(reads);
  putstr(" writes=");
  puthex(writes);
  putstr(" flushes=");
  puthex(flushes);
  putstr(" interrupts=");
  puthex(interrupts);
  putstr(" retired=");
  puthex(retired);
  putstr(" dmagrain=");
  puthex(hal_dma_granule());
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
