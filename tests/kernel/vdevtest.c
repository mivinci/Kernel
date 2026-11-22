#include "ktest.h"
#include "load.h"
#include "virtiouser.h"
#include <device.h>
#include <proc.h>
#include <abi/boot.h>
#include <abi/observe.h>
#include <diag.h>
#include <irq.h>
#include <clock.h>
#include <hal/dma.h>
#include <dma.h>
#include <vm.h>
extern const u8        vdev_image[], vdev_image_end[];
extern const u64       vdev_trace_address;
typedef struct Testdma Testdma;
struct Testdma
{
  Dmaid domain;
  Memid pages[VTEST_PAGES];
};
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
load(Handle pid, Handle memory, const Dtinfo *info, u32 count, const u32 members[VTEST_MEMBERS],
     u64 page, Testdma dma[VTEST_MEMBERS])
{
  struct
  {
    Boot     header;
    Resentry resources[4 + VTEST_MEMBERS];
  } packet;
  u64      trace[VTEST_TRACE];
  u32      i, j, slot;
  Pglim    limits;
  Procinfo process;
  Rtable  *table = proctable(pid);
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = 4 + count;
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
    REQUIRE(!procirqinstall(pid, info->dev[members[i]].irq[0].number, IRQ_BIT_MIN + i,
                            &packet.resources[3 + i].handle));
  }
  packet.resources[3 + count].tag = VTEST_OBSERVE;
  REQUIRE(!localobserve(table, OBSERVE_SOURCE, &packet.resources[3 + count].handle));
  testloadmapped(pid, vdev_image, (u64) (vdev_image_end - vdev_image), &packet, sizeof(packet));
  REQUIRE(!procinfo(pid, &process));
  for (i = 0; i < VTEST_TRACE; i++)
    trace[i] = 0;
  trace[1] = count;
  limits.minpa = info->ram[0].base;
  limits.maxpa_exclusive = limits.minpa + info->ram[0].size;
  limits.align = PAGE_SIZE;
  /* The test loader supplies the QEMU virt identity/coherent policy.
     Each transport owns separate pins even when the MMIO page is shared. */
  for (i = 0; i < count; i++) {
    trace[4 + i] = info->dev[members[i]].reg[0].base - page;
    trace[23 + i] = info->dev[members[i]].reg[0].size;
    REQUIRE(!dmaopen(members[i], process.acct, &limits, 1, &dma[i].domain));
    for (j = 0; j < VTEST_PAGES; j++) {
      Memid pin;
      slot = i * VTEST_PAGES + j;
      REQUIRE(!dmaalloc(dma[i].domain, PAGE_SIZE, NULL, &pin));
      dma[i].pages[j] = pin;
      REQUIRE(!spacemap(process.space, pin, 0, VTEST_DMA_BASE + slot * PAGE_SIZE, PAGE_SIZE,
                        VM_READ | VM_WRITE));
      REQUIRE(!dmaaddress(dma[i].domain, pin, 0, PAGE_SIZE, &trace[VTEST_DMA_TRACE + slot]));
      REQUIRE(!dmasync(dma[i].domain, pin, 0, PAGE_SIZE, HDMA_BEFORE,
                       HDMA_DEVICE_READ | HDMA_DEVICE_WRITE));
      REQUIRE(!memfree(pin));
    }
  }
  REQUIRE(!spacewrite(process.space, vdev_trace_address, trace, sizeof(trace)));
}

static void
loadclient(Handle pid, Imageid server)
{
  struct
  {
    Boot     header;
    Resentry resources[3];
  } packet;
  Rtable *table = proctable(pid);
  u32     i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = ARRAY_SIZE(packet.resources);
  packet.header.resources_off = sizeof(Boot);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = VTEST_BLK_CALL;
  REQUIRE(!localendpoint(table, server, LOCAL_CALL, LOCAL_USE, 0, 0, &packet.resources[1].handle));
  packet.resources[2].tag = BOOT_SELF_ALLOC;
  packet.resources[2].handle = testresource(table, LOCAL_ALLOC);
  testloadmapped(pid, vdev_image, (u64) (vdev_image_end - vdev_image), &packet, sizeof(packet));
}
static void
testsync(u64 base)
{
  void *address = kaddr(base);
  u64   grain = hal_dma_granule();
  REQUIRE(hal_dma_sync(address, 0, HDMA_AFTER, HDMA_DEVICE_WRITE, 1) == -ERR_INVALID);
  if (!grain) {
    REQUIRE(hal_dma_sync(address, PAGE_SIZE, HDMA_AFTER, HDMA_DEVICE_WRITE, 0) == -ERR_NOTSUP);
    return;
  }
  REQUIRE(grain <= PAGE_SIZE && !(PAGE_SIZE % grain));
  REQUIRE(hal_dma_sync(address, grain - 1, HDMA_AFTER, HDMA_DEVICE_WRITE, 0) == -ERR_INVALID);
  REQUIRE(!hal_dma_sync(address, PAGE_SIZE, HDMA_BEFORE, HDMA_DEVICE_WRITE, 0));
  REQUIRE(!hal_dma_sync(address, PAGE_SIZE, HDMA_AFTER, HDMA_DEVICE_WRITE, 0));
}
static void
testpins(Setid set, const Dtinfo *info, u32 device)
{
  Dmaid  domain, other;
  Acctid payer, next;
  Memid  page, oldest, ordinary, cursor = {0};
  Mem    held;
  Set    account;
  Pglim  limits, occupied;
  u64    address, base, second, third;
  u8     sample[2] = {0xa5, 0x5a};
  u32    i, peer, batches;
  int    error;
  limits.minpa = info->ram[0].base;
  limits.maxpa_exclusive = limits.minpa + info->ram[0].size;
  limits.align = PAGE_SIZE;
  for (peer = device + 1; peer < info->ndev && info->dev[peer].kind != DT_VIRTIO; peer++) {
  }
  REQUIRE(peer < info->ndev);
  REQUIRE(!acctcreate(set, DMA_MAX_PAGES + 4, DMA_MAX_PAGES + 4, &payer));
  REQUIRE(!acctcreate(set, 2, 2, &next));
  REQUIRE(!dmaopen(device, payer, &limits, 1, &domain) &&
          dmaopen(device, next, &limits, 1, &other) == -ERR_BUSY);
  REQUIRE(!dmaopen(peer, payer, &limits, 1, &other));
  REQUIRE(!memcreate(payer, PAGE_SIZE, &ordinary));
  address = UINT64_MAX;
  REQUIRE(dmaaddress(domain, ordinary, 0, 1, &address) == -ERR_DENIED && address == UINT64_MAX &&
          dmasync(domain, ordinary, 0, 1, HDMA_BEFORE, HDMA_DEVICE_READ) == -ERR_DENIED &&
          !memfree(ordinary));
  occupied = limits;
  occupied.minpa += PAGE_SIZE;
  occupied.maxpa_exclusive -= PAGE_SIZE;
  occupied.align = 8 * PAGE_SIZE;
  REQUIRE(!dmaalloc(domain, 3 * PAGE_SIZE, &occupied, &oldest) && !mempage(oldest, 0, &base) &&
          base >= occupied.minpa && base <= occupied.maxpa_exclusive - 4 * PAGE_SIZE &&
          !(base & (occupied.align - 1)) && !mempage(oldest, 1, &second) &&
          !mempage(oldest, 2, &third) && second == base + PAGE_SIZE &&
          third == second + PAGE_SIZE && mempage(oldest, 3, &address) == MEM_BAD &&
          !setstat(set, &account) && account.charged == 4);
  REQUIRE(!memwrite(oldest, PAGE_SIZE - 1, sample, sizeof(sample)) &&
          *(const u8 *) kaddr(second - 1) == sample[0] && *(const u8 *) kaddr(second) == sample[1]);
  REQUIRE(!dmaaddress(domain, oldest, PAGE_SIZE - 1, 2, &address) &&
          address == base + PAGE_SIZE - 1);
  cursor = oldest;
  REQUIRE(dmaafter(other, &cursor) == -ERR_DENIED && cursor.generation == oldest.generation &&
          cursor.index == oldest.index);
  cursor.generation = cursor.index = 0;
  testsync(base);
  REQUIRE(dmaaddress(other, oldest, 0, 1, &address) == -ERR_DENIED &&
          dmasync(other, oldest, 0, 1, HDMA_AFTER, HDMA_DEVICE_WRITE) == -ERR_DENIED &&
          dmaaddress(domain, oldest, 3 * PAGE_SIZE - 1, 2, &address) == -ERR_INVALID &&
          address == base + PAGE_SIZE - 1 &&
          !dmasync(domain, oldest, PAGE_SIZE - 1, 2, HDMA_BEFORE, HDMA_DEVICE_WRITE) &&
          dmasync(domain, oldest, 3 * PAGE_SIZE, 1, HDMA_BEFORE, HDMA_DEVICE_WRITE) ==
              -ERR_INVALID &&
          dmasync(domain, oldest, 1, UINT64_MAX, HDMA_BEFORE, HDMA_DEVICE_WRITE) == -ERR_INVALID);
  REQUIRE(!dmaend(other));
  occupied.minpa = base;
  occupied.maxpa_exclusive = base + PAGE_SIZE;
  occupied.align = PAGE_SIZE;
  page = oldest;
  REQUIRE(!dmaopen(peer, payer, &occupied, 1, &other) &&
          dmaalloc(other, PAGE_SIZE, NULL, &page) == -ERR_NOMEM &&
          page.generation == oldest.generation && page.index == oldest.index && !dmaend(other) &&
          !memfree(oldest));
  for (i = 4; i < DMA_MAX_PAGES; i++) {
    if (i == DMA_MAX_PAGES - 2)
      REQUIRE(dmaalloc(domain, 3 * PAGE_SIZE, NULL, &page) == -ERR_LIMIT);
    REQUIRE(!dmaalloc(domain, PAGE_SIZE, NULL, &page) && !memfree(page));
  }
  REQUIRE(dmaalloc(domain, PAGE_SIZE, NULL, &page) == -ERR_LIMIT && !acctclose(payer) &&
          !acctunbind(payer));
  REQUIRE(!memstat(oldest, &held) && held.kind == MEM_DMA && held.refs == 1 && !held.managers &&
          !setstat(set, &account) && account.charged == DMA_MAX_PAGES);
  REQUIRE(dmaaddress(domain, oldest, 0, 1, &address) == -ERR_DENIED &&
          dmasync(domain, oldest, 0, 1, HDMA_BEFORE, HDMA_DEVICE_WRITE) == -ERR_DENIED &&
          !dmasync(domain, oldest, PAGE_SIZE - 1, 1, HDMA_AFTER, HDMA_DEVICE_WRITE));
  /* No DMA is submitted in this census. The real I/O cases below separately
     verify device reset before releasing their test buffers. */
  batches = 0;
  do {
    error = dmaafter(domain, &cursor);
    batches++;
    REQUIRE(error >= 0 && batches <= DMA_MAX_PAGES / 32 && !setstat(set, &account) &&
            account.charged == DMA_MAX_PAGES);
  } while (error);
  REQUIRE(batches == DMA_MAX_PAGES / 32 && !cursor.generation && !memstat(oldest, &held) &&
          held.refs == 1);
  REQUIRE(dmaend(domain) == 1 && dmaalloc(domain, PAGE_SIZE, NULL, &page) == -ERR_STALE &&
          dmaopen(device, next, &limits, 1, &other) == -ERR_BUSY && !memstat(oldest, &held) &&
          dmaafter(domain, &cursor) == -ERR_STALE &&
          dmasync(domain, oldest, 0, 1, HDMA_AFTER, HDMA_DEVICE_WRITE) == -ERR_STALE);
  do {
    error = dmaend(domain);
  } while (error > 0);
  REQUIRE(!error && memstat(oldest, &held) == MEM_BAD && !setstat(set, &account) &&
          !account.charged);
  REQUIRE(!dmaopen(device, next, &limits, 1, &other) && other.generation > domain.generation &&
          dmaend(domain) == -ERR_STALE && dmaalloc(domain, PAGE_SIZE, NULL, &page) == -ERR_STALE);
  REQUIRE(!dmaalloc(other, PAGE_SIZE, NULL, &page) && !dmaend(other) && !memfree(page));
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
  Procinfo   process, client;
  Life       life, clientlife;
  Liferesult result;
  Pgstat     before, after;
  Device     window;
  Handle     pid, clientpid, memory;
  Testdma    dma[VTEST_MEMBERS];
  u64        page, members, trace[VTEST_TRACE];
  u64        started, current, resize, clientrpc;
  u32        devices[VTEST_MEMBERS], interrupts = 0, retired = 0, reclaimed = 0;
  u32 i, j, k, count, id, pages = 0, probes = 0, empty = 0, found = 0, negotiated = 0, resets = 0;
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
      testpins(set, info, i);
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
        REQUIRE(count < VTEST_MEMBERS);
        members |= (u64) 1 << j;
        REQUIRE(info->dev[j].nirq == 1);
        devices[count++] = j;
      }
    REQUIRE(count && !devgroup(page, members, &id));
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid) && !procinfo(pid, &process));
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &clientpid) && !procinfo(clientpid, &client));
    REQUIRE(!procmmioinstallgroup(pid, page, members, 3, &memory));
    load(pid, memory, info, count, devices, page, dma);
    loadclient(clientpid, process.identity);
    REQUIRE(!procpublish(pid) && !procpublish(clientpid) && !clockmono(&started));
    resize = 0;
    do {
      error = procrunone();
      REQUIRE((!error || error == -ERR_WOULD_BLOCK) && !lifestat(process.life, &life));
      REQUIRE(!lifestat(client.life, &clientlife));
      if (clientlife.state == LIFE_ENDED && clientlife.code != 42) {
        putstr("VDEVCLIENT exit=");
        puthex(clientlife.code);
        putstr("\n");
        REQUIRE(clientlife.reason == LIFE_EXIT && clientlife.code == 42);
      }
      REQUIRE(!clockmono(&current) && current - started < UINT64_C(4000000000));
      if (error == -ERR_WOULD_BLOCK) {
        if (!resize) {
          REQUIRE(!spaceread(process.space, vdev_trace_address + VTEST_RESIZE * sizeof(u64),
                             &resize, sizeof(resize)));
          if (resize)
            putstr("VDEVRESIZE ready\n");
        }
        REQUIRE(procidle() >= 0);
      }
    } while (life.state != LIFE_ENDED || clientlife.state != LIFE_ENDED);
    REQUIRE(!spaceread(client.space, vdev_trace_address, trace, sizeof(trace)));
    if (trace[31]) {
      putstr("VDEVCLIENT fail=");
      puthex(trace[31]);
      putstr("\n");
    }
    REQUIRE(!trace[31] && clientlife.reason == LIFE_EXIT && clientlife.code == 42);
    clientrpc = trace[VTEST_BLK_RPC];
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
            trace[22] == 3 * trace[18] + 2 * trace[19] && trace[2] == 38 * trace[18] &&
            trace[3] == 28 * trace[18] &&
            trace[38] == trace[2] + trace[3] + trace[22] + 41 * trace[VTEST_BLK_RPC] &&
            trace[39] == trace[18] + trace[19] && resize == trace[18] &&
            trace[VTEST_CAPACITY] == 2 * trace[20] && trace[VTEST_PARALLEL] == 2 * trace[18] &&
            trace[VTEST_BLK_RPC] == trace[18] && clientrpc == trace[VTEST_BLK_RPC]);
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
    reads += (u32) (trace[22] + trace[VTEST_PARALLEL] + 41 * trace[VTEST_BLK_RPC]);
    writes += (u32) trace[2];
    flushes += (u32) trace[3];
    pages++;
    REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
    while ((error = procclean(clientpid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(client.life, 1, &result));
    for (j = 0; j < count; j++) {
      Mem   held;
      Memid cursor = {0};
      for (k = 0; k < VTEST_PAGES; k++) {
        REQUIRE(!memstat(dma[j].pages[k], &held) && held.kind == MEM_DMA && held.refs == 1 &&
                !held.managers);
      }
      REQUIRE(!dmaafter(dma[j].domain, &cursor) && !cursor.generation);
      REQUIRE(!dmaend(dma[j].domain));
      for (k = 0; k < VTEST_PAGES; k++)
        REQUIRE(memstat(dma[j].pages[k], &held) == MEM_BAD);
      reclaimed += VTEST_PAGES;
    }
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
  putstr(" blkrpc=");
  puthex(writable);
  putstr(" dmagrain=");
  puthex(hal_dma_granule());
  putstr(" dmapages=");
  puthex(reclaimed);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
