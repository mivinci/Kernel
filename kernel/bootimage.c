#include <bootimage.h>
static u64
read64(const u8 *p)
{
  u64 value = 0;
  u32 i;
  for (i = 0; i < 8; i++)
    value |= (u64) p[i] << (8 * i);
  return value;
}
static int
within(u64 offset, u64 bytes, u64 total)
{
  return offset <= total && bytes <= total - offset;
}
static int
overlap(u64 a, u64 n, u64 b, u64 m)
{
  return n && m && a < b + m && b < a + n;
}
void
bootimagesegment(const void *data, const Bootimage *image, u64 index, Bootseg *out)
{
  const u8 *p = (const u8 *) data + image->segments_off + index * sizeof(Bootseg);
  out->file_off = read64(p);
  out->file_bytes = read64(p + 8);
  out->va = read64(p + 16);
  out->mem_bytes = read64(p + 24);
  out->prot = read64(p + 32);
  out->reserved = read64(p + 40);
}
int
bootimagecheck(const void *input, u64 available, Bootimage *out)
{
  const u8 *data = input, *boot;
  Bootimage header;
  Bootseg   segment, previous;
  u64       i, j, table_bytes;
  u32       executable = 0;
  if (!out)
    return -ERR_INVALID;
  out->magic = out->format = out->bytes = out->entry = 0;
  out->segment_count = out->segments_off = out->boot_off = out->boot_bytes = 0;
  if (!data || available < sizeof(Bootimage) || available > ~(u64) 0 - (u64) data)
    return -ERR_INVALID;
  header.magic = read64(data);
  header.format = read64(data + 8);
  header.bytes = read64(data + 16);
  header.entry = read64(data + 24);
  header.segment_count = read64(data + 32);
  header.segments_off = read64(data + 40);
  header.boot_off = read64(data + 48);
  header.boot_bytes = read64(data + 56);
  if (header.magic != BOOT_LOAD_MAGIC || header.format != 1 || header.bytes != available ||
      !header.segment_count || header.segment_count > BOOT_LOAD_SEGMENTS ||
      header.segments_off < sizeof(Bootimage) || (header.segments_off & 7) ||
      header.boot_off < sizeof(Bootimage) || (header.boot_off & 7) ||
      header.boot_bytes < sizeof(Boot) || header.boot_bytes > BOOT_MAX_BYTES)
    return -ERR_INVALID;
  table_bytes = header.segment_count * sizeof(Bootseg);
  if (!within(header.segments_off, table_bytes, available) ||
      !within(header.boot_off, header.boot_bytes, available) ||
      overlap(header.segments_off, table_bytes, header.boot_off, header.boot_bytes))
    return -ERR_INVALID;
  for (i = 0; i < header.segment_count; i++) {
    bootimagesegment(data, &header, i, &segment);
    if ((segment.va & PAGE_MASK) || (segment.mem_bytes & PAGE_MASK) || !segment.mem_bytes ||
        segment.va < BOOT_USER_LO || segment.va >= BOOT_IMAGE_HI ||
        segment.mem_bytes > BOOT_IMAGE_HI - segment.va || segment.reserved || !(segment.prot & 1) ||
        segment.prot & ~7UL || (segment.prot & 6) == 6 || segment.file_bytes > segment.mem_bytes ||
        !within(segment.file_off, segment.file_bytes, available) ||
        overlap(segment.file_off, segment.file_bytes, 0, sizeof(Bootimage)) ||
        overlap(segment.file_off, segment.file_bytes, header.segments_off, table_bytes) ||
        overlap(segment.file_off, segment.file_bytes, header.boot_off, header.boot_bytes))
      return -ERR_INVALID;
    for (j = 0; j < i; j++) {
      bootimagesegment(data, &header, j, &previous);
      if (overlap(segment.va, segment.mem_bytes, previous.va, previous.mem_bytes) ||
          overlap(segment.file_off, segment.file_bytes, previous.file_off, previous.file_bytes))
        return -ERR_INVALID;
    }
    if ((segment.prot & 4) && header.entry >= segment.va &&
        header.entry - segment.va < segment.mem_bytes)
      executable = 1;
  }
  boot = data + header.boot_off;
  if (!executable || (header.entry & 1) || read64(boot) != BOOT_MAGIC ||
      read64(boot + 8) != BOOT_FORMAT || read64(boot + 16) != header.boot_bytes ||
      read64(boot + 24) || read64(boot + 112) != BOOT_USER_LO || read64(boot + 120) != BOOT_USER_HI)
    return -ERR_INVALID;
  out->magic = header.magic;
  out->format = header.format;
  out->bytes = header.bytes;
  out->entry = header.entry;
  out->segment_count = header.segment_count;
  out->segments_off = header.segments_off;
  out->boot_off = header.boot_off;
  out->boot_bytes = header.boot_bytes;
  return 0;
}
