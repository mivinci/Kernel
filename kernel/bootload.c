/* Initial-process loading only. Ordinary ELF decoding remains in libsys. */
#include <bootimage.h>
#include <proc.h>
static Handle
resource(Rtable *table, u32 kind)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  return 0;
}
int
bootimageload(Handle pid, const void *data, u64 bytes, const void *startup, u64 startupbytes)
{
  Bootimage   image;
  Bootseg     segment;
  const Boot *boot = startup;
  const u8   *source = data;
  Rtable     *table;
  Handle      allocator, space, memory;
  Allocarg    args;
  Config      config;
  u64         i, offset, copied;
  int         error;
  if (pid != 1 || !startup || ((u64) startup & 7) || startupbytes < sizeof(Boot) ||
      startupbytes > BOOT_MAX_BYTES)
    return -ERR_INVALID;
  error = bootimagecheck(data, bytes, &image);
  if (error)
    return error;
  if (boot->magic != BOOT_MAGIC || boot->format != BOOT_FORMAT || boot->bytes != startupbytes ||
      boot->flags || boot->user_lo != BOOT_USER_LO || boot->user_hi != BOOT_USER_HI)
    return -ERR_INVALID;
  /* Nonzero package fields require the retained-view admission path. */
  if (boot->package_va || boot->package_bytes)
    return -ERR_INVALID;
  table = proctable(pid);
  if (!table)
    return -ERR_STATE;
  allocator = resource(table, LOCAL_ALLOC);
  space = resource(table, LOCAL_SPACE);
  if (!allocator || !space)
    return -ERR_STATE;
  args.bytes = PAGE_SIZE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  for (i = 0; i < image.segment_count; i++) {
    bootimagesegment(source, &image, i, &segment);
    for (offset = 0; offset < segment.mem_bytes; offset += PAGE_SIZE) {
      args.kind = segment.prot & VM_EXEC ? ALLOC_CODE : ALLOC_DATA;
      error = localalloc(table, allocator, &args, &memory);
      if (error)
        return error;
      if (offset < segment.file_bytes) {
        copied = MIN(PAGE_SIZE, segment.file_bytes - offset);
        error = localwrite(table, memory, 0, source + segment.file_off + offset, copied);
        if (error)
          return error;
      }
      if (segment.prot & VM_EXEC) {
        error = localseal(table, memory);
        if (error)
          return error;
      }
      error = localmap(table, space, memory, 0, segment.va + offset, PAGE_SIZE, segment.prot);
      if (error)
        return error;
      error = localfree(table, memory);
      if (error)
        return error;
    }
  }
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  error = localalloc(table, allocator, &args, &memory);
  if (!error)
    error = localmap(table, space, memory, 0, 0x3ffffe0000UL, args.bytes, VM_READ | VM_WRITE);
  if (!error)
    error = localfree(table, memory);
  if (error)
    return error;
  args.bytes = ALIGN_UP(startupbytes, PAGE_SIZE);
  error = localalloc(table, allocator, &args, &memory);
  if (!error)
    error = localwrite(table, memory, 0, startup, startupbytes);
  if (!error)
    error = localmap(table, space, memory, 0, 0x3fff000000UL, args.bytes, VM_READ);
  if (!error)
    error = localfree(table, memory);
  if (error)
    return error;
  config.entry = image.entry;
  config.stack = 0x3fffff0000UL;
  config.boot = 0x3fff000000UL;
  config.bootsz = startupbytes;
  config.sigmask = config.sigdefault = 0;
  return procconfigure(pid, &config, 0);
}
