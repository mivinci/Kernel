#include "load.h"
#include <bootimage.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("trusted test image loading");                                                         \
  } while (0)
Handle
testresource(Rtable *table, u32 kind)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  panic("missing test boot resource");
}
void
testload(Handle pid, const u8 *data, u64 bytes, const void *boot, u64 bootbytes)
{
  Rtable   *table = proctable(pid);
  Bootimage image;
  Bootseg   decoded;
  Handle    space = testresource(table, LOCAL_SPACE), allocator = testresource(table, LOCAL_ALLOC);
  Handle    memory, stack, startup;
  Allocarg  args;
  Config    config;
  Hpte      pte;
  u64       i, page, copied;
  REQUIRE(!bootimagecheck(data, bytes, &image));
  REQUIRE(bootbytes >= sizeof(Boot) && bootbytes <= BOOT_MAX_BYTES);
  args.bytes = PAGE_SIZE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  for (i = 0; i < image.segment_count; i++) {
    const Bootseg *segment = &decoded;
    bootimagesegment(data, &image, i, &decoded);
    for (page = 0; page < segment->mem_bytes; page += PAGE_SIZE) {
      args.kind = segment->prot & VM_EXEC ? ALLOC_CODE : ALLOC_DATA;
      REQUIRE(!localalloc(table, allocator, &args, &memory));
      if (page < segment->file_bytes) {
        copied = MIN(PAGE_SIZE, segment->file_bytes - page);
        REQUIRE(!localwrite(table, memory, 0, data + segment->file_off + page, copied));
      }
      if (segment->prot & VM_EXEC)
        REQUIRE(!localseal(table, memory));
      REQUIRE(!localmap(table, space, memory, 0, segment->va + page, PAGE_SIZE, segment->prot));
      REQUIRE(spacelookup(table->space, segment->va + page, &pte) &&
              pte.prot == (segment->prot | VM_USER) && pte.attr == VM_NORMAL);
    }
  }
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(table, allocator, &args, &stack));
  REQUIRE(!localmap(table, space, stack, 0, 0x3ffffe0000UL, args.bytes, VM_READ | VM_WRITE));
  args.bytes = ALIGN_UP(bootbytes, PAGE_SIZE);
  REQUIRE(!localalloc(table, allocator, &args, &startup));
  REQUIRE(!localwrite(table, startup, 0, boot, bootbytes));
  REQUIRE(!localmap(table, space, startup, 0, 0x3fff000000UL, args.bytes, VM_READ));
  config.entry = image.entry;
  config.stack = 0x3fffff0000UL;
  config.boot = 0x3fff000000UL;
  config.bootsz = bootbytes;
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &config, 0));
}
