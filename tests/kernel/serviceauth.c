/* Native authority/startup/shared-memory exercise; no service READY is sent. */
#include <sys/boot.h>
#include <abi/resource.h>
#include <abi/system.h>
#include "private/start.h"
#include "private/syscall.h"
#include "private/io.h"
#include <abi/syscall.h>
volatile u64 handoffproof[8];

#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return n;                                                                                    \
  } while (0)

static Handle system, memory, source;
static u64    windowbytes;
static u64    uartkind, uartoffset, uartbytes;
static u64    uartshift, uartwidth;
static void
regwrite(u64 base, u32 offset, u32 value)
{
  u64 address = base + ((u64) offset << uartshift);
  if (uartwidth == 4)
    iowrite32(address, value);
  else
    iowrite8(address, value);
}
static int
handoff(const Bootview *boot)
{
  Maparg mapping;
  Sysret result;
  u64    args[7], i, turns, base;
  isize  error;
  REQUIRE(__sys_debughandoff(0) == -ERR_BAD_HANDLE &&
              __sys_debughandoff(bootresource(boot, BOOT_SELF_SPACE)) == -ERR_DENIED,
          180);
  for (i = 0; i < 7; i++)
    args[i] = 0;
  args[0] = (u64) system;
  args[1] = 1;
  REQUIRE(__sys_enter(SYS_DEBUGHANDOFF, args, &result) == -ERR_INVALID, 181);
  for (i = 0; i < 8; i++)
    REQUIRE(!((u64 *) &result)[i], 182);
  for (turns = 0; turns < 64; turns++) {
    error = __sys_debughandoff(system);
    if (!error)
      break;
    REQUIRE(error == -ERR_BUSY && !__sys_yield(), 183);
  }
  REQUIRE(turns < 64 && !__sys_debughandoff(system), 184);
  handoffproof[0] = 1;
  handoffproof[1] = turns + 1;
  handoffproof[2] = uartkind;
  mapping.mem = memory;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = windowbytes;
  mapping.prot = MAP_READ | MAP_WRITE;
  mapping.reserved = 0;
  REQUIRE(!__sys_map(bootresource(boot, BOOT_SELF_SPACE), &mapping), 185);
  base = mapping.address + uartoffset;
  /* Fatal-only recovery must restore the captured configuration, not depend
     on the outgoing driver's DLAB/divisor/format/enabled state. */
  if (uartkind == BOOT_UART_NS16550) {
    regwrite(base, 3, 0x80);
    regwrite(base, 0, 0);
    regwrite(base, 1, 0);
  } else {
    regwrite(base, 0x30, 0);
    regwrite(base, 0x24, 1);
    regwrite(base, 0x28, 0);
    regwrite(base, 0x2c, 0);
  }
  REQUIRE(!__sys_debughandoff(system), 186);
  handoffproof[3] = 1;
  REQUIRE(!__sys_unmap(bootresource(boot, BOOT_SELF_SPACE), mapping.address), 187);
  return 0;
}
extern int serialparent(Handle creator, Handle listener, Handle mmio, Handle irq, u64 kind,
                        u64 offset, u64 bytes, u64 shift, u64 width);
static int
inventory(const Bootview *boot, u64 image)
{
  Queryrec row;
  Maparg   mapping;
  u64      written = 99, next = 99, i;
  system = bootresource(boot, BOOT_SYSTEM);
  REQUIRE(system > 0, 120);
  for (i = 0; i < sizeof(row); i++)
    ((u8 *) &row)[i] = 0xa5;
  REQUIRE(__sys_query(0, QUERY_BOOT_RESOURCE, 0, &row, 1, &written, &next) == -ERR_DENIED &&
              !written && !next,
          121);
  for (i = 0; i < sizeof(row); i++)
    REQUIRE(((const u8 *) &row)[i] == 0xa5, 122);
  REQUIRE(!__sys_query(system, QUERY_BOOT_RESOURCE, 0, &row, 1, &written, &next) && written == 1 &&
              !next && row.type == QUERY_BOOT_RESOURCE && row.pid == 1 && row.image == image &&
              row.stamp == 1 && row.data[0] && row.data[4] &&
              (row.data[1] == BOOT_UART_NS16550 || row.data[1] == BOOT_UART_PL011) && row.data[2] &&
              row.data[3] && !row.data[5],
          123);
  for (i = 8; i < 20; i++)
    REQUIRE(!row.data[i], 124);
  memory = (Handle) row.data[0];
  source = (Handle) row.data[4];
  windowbytes = ((row.data[2] & 4095) + row.data[3] + 4095) & ~4095UL;
  uartkind = row.data[1];
  uartoffset = row.data[2] & PAGE_MASK;
  uartbytes = row.data[3];
  uartshift = row.data[6];
  uartwidth = row.data[7];
  REQUIRE(uartshift <= 4 && (uartwidth == 1 || uartwidth == 4) && !(uartoffset & (uartwidth - 1)) &&
              (uartkind == BOOT_UART_PL011 ? (!uartshift && uartwidth == 4)
                                           : !((1UL << uartshift) & (uartwidth - 1))),
          143);
  REQUIRE(__sys_query(system, QUERY_BOOT_RESOURCE, 0x20001, &row, 1, &written, &next) ==
                  -ERR_STALE &&
              !written && !next,
          125);
  REQUIRE(__sys_query(system, QUERY_BOOT_RESOURCE, 0, NULL, 1, &written, &next) == -ERR_INVALID &&
              !written && !next,
          126);
  REQUIRE(__sys_query(system, QUERY_BOOT_RESOURCE, 0, (Queryrec *) 0x3000000, 1, &written, &next) ==
                  -ERR_FAULT &&
              !written && !next,
          142);
  REQUIRE(__sys_inherit(system, 1) == -ERR_DENIED && __sys_inherit(source, 1) == -ERR_DENIED, 127);
  mapping.mem = memory;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = windowbytes;
  mapping.prot = 3;
  mapping.reserved = 0;
  REQUIRE(!__sys_map(bootresource(boot, BOOT_SELF_SPACE), &mapping) &&
              !__sys_unmap(bootresource(boot, BOOT_SELF_SPACE), mapping.address),
          128);
  return 0;
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          creator, listener = 0, view, pid, control, installed;
  Handle          self, parent, device, binding;
  u64 image, cursor = 0, written, next, i, creates = 0, found = 0, systems = 0, memories = 0,
             sources = 0;
  Queryrec  records[8], own, child;
  Createarg args;
  isize     error;
  (void) argv;
  REQUIRE(argc == 1 && !__sys_getid(&self, &image, &parent) && self == 1 && !parent, 100);
  error = inventory(boot, image);
  if (error)
    return (int) error;
  creator = bootresource(boot, BOOT_CREATE);
  REQUIRE(creator > 0, 101);
  do {
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, records, 4, &written, &next), 102);
    for (i = 0; i < written; i++) {
      if (records[i].data[1] == RESOURCE_CREATE) {
        creates++;
        REQUIRE((records[i].data[2] & (CREATE_RIGHTS | (1UL << 63))) ==
                    (CREATE_RIGHTS | (1UL << 63)),
                103);
        if ((Handle) records[i].data[0] == creator)
          found++;
      }
      if (records[i].data[1] == RESOURCE_CALL && records[i].data[6] == 1 &&
          records[i].data[7] == image && (records[i].data[2] & (1UL << 63))) {
        REQUIRE(!listener, 104);
        listener = (Handle) records[i].data[0];
      }
      if (records[i].data[1] == RESOURCE_SYSTEM) {
        REQUIRE((Handle) records[i].data[0] == system &&
                    records[i].data[2] == (SYSTEM_QUERY | SYSTEM_CONSOLE) && !records[i].data[5],
                129);
        systems++;
      }
      if ((Handle) records[i].data[0] == memory) {
        REQUIRE(records[i].data[1] == RESOURCE_MEM && records[i].data[2] == ((1UL << 63) | 3) &&
                    !records[i].data[3] && records[i].data[4] == windowbytes && !records[i].data[5],
                130);
        memories++;
      }
      if ((Handle) records[i].data[0] == source) {
        REQUIRE(records[i].data[1] == RESOURCE_IRQ_SOURCE && records[i].data[2] == 1 &&
                    !records[i].data[5],
                131);
        sources++;
      }
    }
    cursor = next;
  } while (cursor);
  REQUIRE(creates == 16 && found == 1 && listener && systems == 1 && memories == 1 && sources == 1,
          105);
  REQUIRE(!__sys_query(0, QUERY_ACCT, 0, &own, 1, &written, &next) && written == 1 && !next, 106);
  error = serialparent(creator, listener, memory, source, uartkind, uartoffset, uartbytes,
                       uartshift, uartwidth);
  if (error)
    return (int) error;
  args.memlimit = 64;
  args.exec_limit = 128;
  args.resources = 8;
  args.pending = 1;
  args.ruid = args.euid = args.rgid = args.egid = 0;
  args.reserved = 0;
  REQUIRE(!__sys_create(creator, &args, &view, &pid, &control) && view > 0 && pid > 1 &&
              control > 0,
          107);
  REQUIRE(!__sys_query(control, QUERY_ACCT, 0, &child, 1, &written, &next) && written == 1 &&
              !next && child.data[0] != own.data[0] && child.data[1] == 64 && child.data[2] == 128,
          108);
  REQUIRE(!__sys_grant(view, listener, 1, 0, 0, 0, &installed) && installed > 0, 109);
  REQUIRE(__sys_grant(view, system, 1, 0, 0, 0, &device) == -ERR_DENIED && !device, 132);
  REQUIRE(__sys_grant(view, source, 1, 0, 0, 0, &device) == -ERR_DENIED && !device, 133);
  REQUIRE(__sys_query(control, QUERY_BOOT_RESOURCE, 0, records, 1, &written, &next) ==
                  -ERR_DENIED &&
              !written && !next,
          134);
  REQUIRE(!__sys_grant(view, memory, 3, 0, windowbytes, 0, &device) && device > 0, 135);
  REQUIRE(!__sys_irqbind(control, source, 16, &binding) && binding > 0, 136);
  REQUIRE(!__sys_free(source), 137);
  REQUIRE(__sys_query(system, QUERY_BOOT_RESOURCE, 0, records, 1, &written, &next) == -ERR_STALE &&
              !written && !next,
          138);
  REQUIRE(!__sys_irqunbind(control, binding), 139);
  REQUIRE(!__sys_query(view, QUERY_RESOURCE, 0, records, 8, &written, &next) && !next, 110);
  found = 0;
  for (i = 0; i < written; i++)
    if ((Handle) records[i].data[0] == installed) {
      REQUIRE(records[i].data[1] == RESOURCE_CALL && records[i].data[2] == 1 &&
                  records[i].data[6] == 1 && records[i].data[7] == image,
              111);
      found++;
    }
  REQUIRE(found == 1, 112);
  REQUIRE(!__sys_destroy(view) && !__sys_free(control), 113);
  error = __sys_wait(-1, WAIT_WNOHANG, &pid, &image, &cursor, &next);
  REQUIRE(error == -ERR_CHILD, 114);
  error = handoff(boot);
  if (error)
    return (int) error;
  REQUIRE(!__sys_free(system) && !__sys_free(memory), 140);
  REQUIRE(__sys_query(system, QUERY_BOOT_RESOURCE, 0, records, 1, &written, &next) ==
                  -ERR_BAD_HANDLE &&
              !written && !next,
          141);
  return 42;
}
