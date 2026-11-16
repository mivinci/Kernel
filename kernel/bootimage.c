#include <bootimage.h>
static int
within(u64 offset, u64 bytes, u64 total)
{
  return offset <= total && bytes <= total - offset;
}
int
bootimagecheck(const void *data, u64 available, const Bootimage **out)
{
  const Bootimage *header = data;
  const Bootseg   *segments;
  const Boot      *boot;
  u64              i, j, table_bytes, boot_end;
  u32              executable = 0;
  if (!out)
    return -ERR_INVALID;
  *out = NULL;
  if (!data || ((u64) data & 7) || available < sizeof(Bootimage))
    return -ERR_INVALID;
  if (header->magic != BOOT_LOAD_MAGIC || header->format != 1 || header->bytes != available ||
      !header->segment_count || header->segment_count > BOOT_LOAD_SEGMENTS ||
      header->segments_off < sizeof(Bootimage) || (header->segments_off & 7) ||
      header->boot_off < sizeof(Bootimage) || (header->boot_off & 7) ||
      header->boot_bytes < sizeof(Boot) || header->boot_bytes > BOOT_MAX_BYTES)
    return -ERR_INVALID;
  table_bytes = header->segment_count * sizeof(Bootseg);
  if (!within(header->segments_off, table_bytes, available) ||
      !within(header->boot_off, header->boot_bytes, available))
    return -ERR_INVALID;
  boot_end = header->boot_off + header->boot_bytes;
  if (header->segments_off < boot_end && header->boot_off < header->segments_off + table_bytes)
    return -ERR_INVALID;
  segments = (const Bootseg *) ((const u8 *) data + header->segments_off);
  for (i = 0; i < header->segment_count; i++) {
    const Bootseg *segment = &segments[i];
    if ((segment->va & PAGE_MASK) || (segment->mem_bytes & PAGE_MASK) || !segment->mem_bytes ||
        segment->va < BOOT_USER_LO || segment->va >= 0x3fff000000UL ||
        segment->mem_bytes > 0x3fff000000UL - segment->va || segment->reserved ||
        !(segment->prot & 1) || segment->prot & ~7UL || (segment->prot & 6) == 6 ||
        segment->file_bytes > segment->mem_bytes ||
        !within(segment->file_off, segment->file_bytes, available))
      return -ERR_INVALID;
    if (segment->file_bytes && (segment->file_off < sizeof(Bootimage) ||
                                (segment->file_off < header->segments_off + table_bytes &&
                                 header->segments_off < segment->file_off + segment->file_bytes) ||
                                (segment->file_off < boot_end &&
                                 header->boot_off < segment->file_off + segment->file_bytes)))
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (segment->va < segments[j].va + segments[j].mem_bytes &&
          segments[j].va < segment->va + segment->mem_bytes)
        return -ERR_INVALID;
    if ((segment->prot & 4) && header->entry >= segment->va &&
        header->entry - segment->va < segment->mem_bytes)
      executable = 1;
  }
  boot = (const Boot *) ((const u8 *) data + header->boot_off);
  if (!executable || (header->entry & 1) || boot->magic != BOOT_MAGIC ||
      boot->format != BOOT_FORMAT || boot->bytes != header->boot_bytes || boot->flags ||
      boot->user_lo != BOOT_USER_LO || boot->user_hi != BOOT_USER_HI)
    return -ERR_INVALID;
  *out = header;
  return 0;
}
