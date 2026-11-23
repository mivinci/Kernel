/* Trusted setup and cleanup around real PID1 and production blk. */
#include "blkserviceuser.h"
#include "load.h"
#include <abi/boot.h>
#include <abi/mem.h>
#include <abi/observe.h>
#include <acct.h>
#include <clock.h>
#include <device.h>
#include <diag.h>
#include <dma.h>
#include <hal/dma.h>
#include <irq.h>
#include <life.h>
#include <local.h>
#include <page.h>
#include <proc.h>
#include <space.h>
#include <vm.h>

#define BLK_TAG_OBSERVE UINT64_C(256)
#define BLK_TAG_INIT    UINT64_C(257)
#define BLK_TAG_MMIO    UINT64_C(258)
#define BLK_TAG_IRQ     UINT64_C(259)
#define BLK_TAG_DMA     UINT64_C(260)
#define BLK_DMA_PAGES   9
#define BLK_TIMEOUT_NS  UINT64_C(30000000000)

extern const u8  blkserviceinit_image[], blkserviceinit_image_end[];
extern const u8  blk_service[], blk_service_end[];
extern const u64 blkserviceinittrace_address, blkdiag_address;
static u64       blkdiagbackup[8];
static void      clear(void *, u64);

typedef struct Blkfixture Blkfixture;
typedef struct Blkpacket  Blkpacket;
struct Blkfixture
{
  Setid    set, initset;
  Dmaid    dma;
  Memid    dmamem;
  u64      addresses[BLK_DMA_PAGES];
  Handle   init, service, irq, mmio, initcall;
  Handle   observe, initresource, dmahandle;
  Procinfo initinfo, serviceinfo;
  u64      device, page, members;
  u32      window;
};
struct Blkpacket
{
  Boot     header;
  Strref   args[16];
  Resentry resources[8];
  char     strings[256];
};

static Blkpacket initpacket, servicepacket;

#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("BLKSERVICEFAIL line=");                                                              \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("standalone block service");                                                           \
    }                                                                                              \
  } while (0)

static void
backupdiag(Spaceid space)
{
  isize error = spaceread(space, blkdiag_address, blkdiagbackup, sizeof(blkdiagbackup));
  if (error)
    clear(blkdiagbackup, sizeof(blkdiagbackup));
}

static void
clear(void *data, u64 bytes)
{
  volatile u8 *out = data;
  while (bytes--)
    *out++ = 0;
}

static void
packetinit(Blkpacket *packet, u64 argc, u64 resources)
{
  clear(packet, sizeof(*packet));
  packet->header.magic = BOOT_MAGIC;
  packet->header.format = BOOT_FORMAT;
  packet->header.bytes = sizeof(*packet);
  packet->header.argc = argc;
  packet->header.argv_off = argc ? offsetof(Blkpacket, args) : 0;
  packet->header.resource_count = resources;
  packet->header.resources_off = offsetof(Blkpacket, resources);
  packet->header.user_lo = BOOT_USER_LO;
  packet->header.user_hi = BOOT_USER_HI;
}

static void
stringarg(Blkpacket *packet, u32 index, const char *text, u32 *used)
{
  usize length = 0;
  usize i;
  while (text[length])
    length++;
  length++;
  REQUIRE(length <= sizeof(packet->strings) - *used);
  packet->args[index].offset = (u32) offsetof(Blkpacket, strings) + *used;
  packet->args[index].length = (u32) length;
  clear(packet->strings + *used, length);
  for (i = 0; i < length; i++)
    packet->strings[*used + i] = text[i];
  *used += (u32) length;
}

static void
numberarg(Blkpacket *packet, u32 index, u64 value, u32 *used)
{
  char text[21];
  u32  position = 20;
  text[position] = 0;
  do {
    text[--position] = (char) ('0' + value % 10);
    value /= 10;
  } while (value);
  stringarg(packet, index, text + position, used);
}

static void
resource(Resentry *entry, u64 tag, Handle handle)
{
  entry->tag = tag;
  entry->handle = handle;
  entry->flags = entry->reserved = 0;
}

static void
finddevice(const Dtinfo *hardware, Blkfixture *fixture)
{
  u64 members;
  u32 i, first = DT_DEVICES, count = 0;
  for (i = 0; i < hardware->ndev; i++)
    if (hardware->dev[i].kind == DT_VIRTIO && hardware->dev[i].nreg &&
        hardware->dev[i].reg[0].size >= 0x0c &&
        (first == DT_DEVICES || hardware->dev[i].reg[0].base < hardware->dev[first].reg[0].base)) {
      first = i;
      fixture->page = ALIGN_DOWN(hardware->dev[i].reg[0].base, PAGE_SIZE);
    }
  REQUIRE(first < DT_DEVICES);
  members = 0;
  for (i = 0; i < hardware->ndev; i++)
    if (hardware->dev[i].kind == DT_VIRTIO &&
        ALIGN_DOWN(hardware->dev[i].reg[0].base, PAGE_SIZE) == fixture->page) {
      REQUIRE(count < 8);
      members |= (u64) 1 << i;
      count++;
    }
  REQUIRE(count && !devgroup(fixture->page, members, &fixture->window));
  fixture->device = first;
  fixture->members = members;
}

static void
setupdma(const Dtinfo *hardware, Blkfixture *fixture)
{
  Pglim limits;
  u32   page;
  limits.minpa = hardware->ram[0].base;
  limits.maxpa_exclusive = limits.minpa + hardware->ram[0].size;
  limits.align = PAGE_SIZE;
  REQUIRE(!dmaopen((u32) fixture->device, fixture->serviceinfo.acct, &limits, 1, &fixture->dma));
  REQUIRE(!dmaalloc(fixture->dma, BLK_DMA_PAGES * PAGE_SIZE, NULL, &fixture->dmamem));
  for (page = 0; page < BLK_DMA_PAGES; page++) {
    REQUIRE(!dmaaddress(fixture->dma, fixture->dmamem, page * PAGE_SIZE, PAGE_SIZE,
                        &fixture->addresses[page]));
    REQUIRE(!dmasync(fixture->dma, fixture->dmamem, page * PAGE_SIZE, PAGE_SIZE, HDMA_BEFORE,
                     HDMA_DEVICE_READ | HDMA_DEVICE_WRITE));
  }
  REQUIRE(!localmem(proctable(fixture->service), fixture->dmamem, VM_READ | VM_WRITE, 0,
                    BLK_DMA_PAGES * PAGE_SIZE, 0, &fixture->dmahandle));
  REQUIRE(!memfree(fixture->dmamem));
}

static void
setupchildren(const Dtinfo *hardware, Blkfixture *fixture)
{
  u32 used, page;
  finddevice(hardware, fixture);
  REQUIRE(!procnew(fixture->set, fixture->init, 512, 1024, LOCALS, &fixture->service) &&
          !procinfo(fixture->service, &fixture->serviceinfo) &&
          !localobserve(proctable(fixture->service), OBSERVE_SOURCE, &fixture->observe));
  REQUIRE(!procmmioinstallgroup(fixture->service, fixture->page, fixture->members,
                                VM_READ | VM_WRITE, &fixture->mmio) &&
          !procirqinstall(fixture->service, hardware->dev[fixture->device].irq[0].number,
                          IRQ_BIT_MIN, &fixture->irq) &&
          !localendpoint(proctable(fixture->service), fixture->initinfo.identity, LOCAL_CALL,
                         LOCAL_USE | LOCAL_GRANT, 0, 0, &fixture->initresource) &&
          !localendpoint(proctable(fixture->init), fixture->serviceinfo.identity, LOCAL_CALL,
                         LOCAL_USE, 0, 0, &fixture->initcall));
  setupdma(hardware, fixture);
  packetinit(&initpacket, 2, 3);
  packetinit(&servicepacket, 5 + BLK_DMA_PAGES, 7);
  resource(&initpacket.resources[0], BOOT_SELF_SPACE,
           testresource(proctable(fixture->init), LOCAL_SPACE));
  resource(&initpacket.resources[1], BOOT_SELF_ALLOC,
           testresource(proctable(fixture->init), LOCAL_ALLOC));
  resource(&initpacket.resources[2], BLKSERVICE_INIT_CALL, fixture->initcall);
  resource(&servicepacket.resources[0], BOOT_SELF_SPACE,
           testresource(proctable(fixture->service), LOCAL_SPACE));
  resource(&servicepacket.resources[1], BOOT_SELF_ALLOC,
           testresource(proctable(fixture->service), LOCAL_ALLOC));
  resource(&servicepacket.resources[2], BLK_TAG_OBSERVE, fixture->observe);
  resource(&servicepacket.resources[3], BLK_TAG_INIT, fixture->initresource);
  resource(&servicepacket.resources[4], BLK_TAG_MMIO, fixture->mmio);
  resource(&servicepacket.resources[5], BLK_TAG_IRQ, fixture->irq);
  used = 0;
  stringarg(&initpacket, 0, "tests/blk-init", &used);
  numberarg(&initpacket, 1, fixture->service, &used);
  REQUIRE(initpacket.args[1].length >= 2 &&
          ((const char *) &initpacket)[initpacket.args[1].offset] >= '1' &&
          ((const char *) &initpacket)[initpacket.args[1].offset] <= '9');
  used = 0;
  stringarg(&servicepacket, 0, "sbin/blk", &used);
  numberarg(&servicepacket, 1, 7, &used);
  numberarg(&servicepacket, 2, fixture->device, &used);
  numberarg(&servicepacket, 3, hardware->dev[fixture->device].reg[0].base - fixture->page, &used);
  numberarg(&servicepacket, 4, hardware->dev[fixture->device].reg[0].size, &used);
  for (page = 0; page < BLK_DMA_PAGES; page++)
    numberarg(&servicepacket, 5 + page, fixture->addresses[page], &used);
  resource(&servicepacket.resources[6], BLK_TAG_DMA, fixture->dmahandle);
  putstr("BLKSERVICELOAD init\n");
  testload(fixture->init, blkserviceinit_image,
           (u64) (blkserviceinit_image_end - blkserviceinit_image), &initpacket,
           sizeof(initpacket));
  putstr("BLKSERVICELOAD service\n");
  testloadmapped(fixture->service, blk_service, (u64) (blk_service_end - blk_service),
                 &servicepacket, sizeof(servicepacket));
  putstr("BLKSERVICELOAD done\n");
}

void
blkservicetest(const Dtinfo *hardware)
{
  Blkfixture fixture;
  Set        account;
  Pgstat     before, after;
  Life       life;
  Device     window;
  Memid      aftercursor = {0};
  Mem        memory;
  Space      rootspace;
  Kstack     stack;
  u32        page;
  u64        trace[16], start, current;
  int        error;
  clear(&fixture, sizeof(fixture));
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &fixture.set) &&
          !setcreate(8192, 16384, 1, &fixture.initset) &&
          !procboot(fixture.initset, 512, 2048, LOCALS, &fixture.init) && fixture.init == 1 &&
          !procinfo(fixture.init, &fixture.initinfo));
  setupchildren(hardware, &fixture);
  REQUIRE(!procpublish(fixture.init) && !procpublish(fixture.service) && !proctimerstart());
  REQUIRE(!clockmono(&start));
  for (;;) {
    error = procrunone();
    REQUIRE((!error || error == -ERR_WOULD_BLOCK) && !clockmono(&current) &&
            current - start < BLK_TIMEOUT_NS);
    if (error == -ERR_WOULD_BLOCK)
      REQUIRE(procidle() >= 0);
    backupdiag(fixture.serviceinfo.space);
    if (!lifestat(fixture.serviceinfo.life, &life) && life.state == LIFE_ENDED) {
      if (life.reason != LIFE_EXIT || life.code) {
        putstr("BLKSERVICEFAILED service_reason=");
        puthex(life.reason);
        putstr(" code=");
        puthex(life.code);
        putstr(" diag_phase=");
        puthex(blkdiagbackup[0]);
        putstr(" diag_stage=");
        puthex(blkdiagbackup[6] >> 32);
        putstr(" diag_error=");
        puthex(blkdiagbackup[1]);
        putstr("\n");
        panic("block service exited before successful shutdown");
      }
    }
    clear(trace, sizeof(trace));
    if (!spaceread(fixture.initinfo.space, blkserviceinittrace_address, trace, sizeof(trace)) &&
        trace[15]) {
      putstr("BLKSERVICEFAILED init_stage=");
      puthex(trace[14]);
      putstr(" detail=");
      puthex(trace[13]);
      putstr(" map_error=");
      puthex(trace[12]);
      putstr(" reply_seq=");
      puthex(trace[11]);
      putstr(" slot_state=");
      puthex(trace[10]);
      putstr(" marker=");
      puthex(trace[15]);
      putstr("\n");
    }
    REQUIRE(!spaceread(fixture.initinfo.space, blkserviceinittrace_address, trace, sizeof(trace)) &&
            !trace[15]);
    if (trace[9] == 1 && trace[10] == 1 && trace[11] == 1 &&
        procinfo(fixture.service, &fixture.serviceinfo) == -ERR_STALE)
      break;
    REQUIRE(procwork(8) >= 0);
  }
  clear(trace, sizeof(trace));
  REQUIRE(!spaceread(fixture.initinfo.space, blkserviceinittrace_address, trace, sizeof(trace)));
  REQUIRE(!trace[15] && trace[0] == (u64) fixture.service && trace[1] == 7 &&
          trace[2] == fixture.serviceinfo.image && trace[3] && trace[4] == BLKSERVICE_BYTES &&
          trace[5] == BLKSERVICE_BLOCK && trace[6] && trace[7] && trace[8] && trace[9] &&
          trace[10] && trace[11]);
  REQUIRE(!setstat(fixture.initset, &account) && account.charged && account.instances == 1 &&
          !spacestat(fixture.initinfo.space, &rootspace) &&
          !kstackstat(fixture.initinfo.stack, &stack) && stack.state && stack.refs);
  putstr("INITRETAIN charged=");
  puthex(account.charged);
  putstr(" root=");
  puthex(rootspace.root);
  putstr(" stack_table=");
  puthex(stack.table);
  putstr(" stack_va=");
  puthex(KSTACK_BASE + (u64) fixture.initinfo.stack.index * KSTACK_STRIDE + PAGE_SIZE);
  putstr("\n");
  for (page = 0; page < KSTACK_PAGES; page++) {
    putstr("INITSTACK index=");
    puthex(page);
    putstr(" pa=");
    puthex(stack.pa[page]);
    putstr("\n");
  }
  REQUIRE(!memstat(fixture.dmamem, &memory) && memory.pages == BLK_DMA_PAGES && memory.refs == 1 &&
          !memory.managers);
  REQUIRE(!dmaafter(fixture.dma, &aftercursor) && !aftercursor.generation && !dmaend(fixture.dma));
  REQUIRE(memstat(fixture.dmamem, &memory) == MEM_BAD);
  REQUIRE(!devstat(fixture.device * DT_REGS, &window) && !window.refs && !window.mappings &&
          !devstat(fixture.window, &window) && !window.refs && !window.mappings);
  REQUIRE(!setstat(fixture.set, &account) && !account.charged && !account.instances &&
          !setclose(fixture.set));
  pgstat(&after);
  REQUIRE(after.allocated >= before.allocated && after.free <= before.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !irqcheck() && !devcheck());
  putstr("BLKSERVICECHECK ready=");
  puthex(1);
  putstr(" client=");
  puthex(42);
  putstr(" shutdown=");
  puthex(1);
  putstr(" service=");
  puthex(0);
  putstr(" reset=");
  puthex(1);
  putstr(" dma=");
  puthex(BLK_DMA_PAGES);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
