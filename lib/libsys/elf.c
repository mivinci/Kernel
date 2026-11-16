#include "private/elf.h"
static u64
word(const u8 *bytes, u32 count)
{
  u64 value = 0;
  u32 i;
  for (i = 0; i < count; i++)
    value |= (u64) bytes[i] << (i * 8);
  return value;
}
static int
within(u64 offset, u64 bytes, u64 total)
{
  return offset <= total && bytes <= total - offset;
}
isize
elfread(const Elfsource *source, u64 offset, void *destination, u64 bytes)
{
  if (!source || !source->read || bytes > PAGE_SIZE || !within(offset, bytes, source->bytes))
    return -ERR_INVALID;
  return bytes ? source->read(source->context, offset, destination, bytes) : 0;
}
static void
segmentcopy(Elfseg *to, const Elfseg *from)
{
  to->offset = from->offset;
  to->va = from->va;
  to->filesz = from->filesz;
  to->memsz = from->memsz;
  to->prot = from->prot;
}
isize
elfcheck(const Elfsource *source, u64 max_pages, Elfplan *out)
{
  u8     header[64], ph[56];
  Elfseg segment, *previous;
  u64    phoff, phnum, entry, kind, flags, align, page_end = 0, begin, end, pages = 0;
  u32    i, j, executable = 0;
  isize  error;
  if (!out)
    return -ERR_INVALID;
  out->entry = out->pages = 0;
  out->count = out->reserved = 0;
  if (!max_pages)
    return -ERR_INVALID;
  error = elfread(source, 0, header, sizeof(header));
  if (error)
    return error;
  if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' ||
      header[4] != 2 || header[5] != 1 || header[6] != 1 || word(header + 16, 2) != 2 ||
      word(header + 18, 2) != elfmachine() || word(header + 20, 4) != 1 ||
      word(header + 52, 2) != 64 || word(header + 54, 2) != sizeof(ph))
    return -ERR_INVALID;
  entry = word(header + 24, 8);
  phoff = word(header + 32, 8);
  phnum = word(header + 56, 2);
  if (!phnum || phnum > ELF_PHNUM || phoff < sizeof(header) ||
      !within(phoff, phnum * sizeof(ph), source->bytes) || (entry & (elfentryalign() - 1)))
    return -ERR_INVALID;
  for (i = 0; i < phnum; i++) {
    error = elfread(source, phoff + i * sizeof(ph), ph, sizeof(ph));
    if (error)
      return error;
    kind = word(ph, 4);
    flags = word(ph + 4, 4);
    if (kind == 2 || kind == 3 || (kind == 0x6474e551 && (flags & 1)))
      return -ERR_INVALID;
    if (kind != 1)
      continue;
    segment.offset = word(ph + 8, 8);
    segment.va = word(ph + 16, 8);
    segment.filesz = word(ph + 32, 8);
    segment.memsz = word(ph + 40, 8);
    align = word(ph + 48, 8);
    if (segment.filesz > segment.memsz || !within(segment.offset, segment.filesz, source->bytes) ||
        (align > 1 && ((align & (align - 1)) || segment.va % align != segment.offset % align)) ||
        (flags & ~7UL) || !(flags & 4))
      return -ERR_INVALID;
    if (!segment.memsz)
      continue;
    if (segment.va < ELF_LOAD_LO || segment.va >= ELF_LOAD_HI ||
        segment.memsz > ELF_LOAD_HI - segment.va)
      return -ERR_INVALID;
    segment.prot = MAP_READ | (flags & 2 ? MAP_WRITE : 0) | (flags & 1 ? MAP_EXEC : 0);
    if ((segment.prot & (MAP_WRITE | MAP_EXEC)) == (MAP_WRITE | MAP_EXEC))
      return -ERR_INVALID;
    for (j = 0; j < out->count; j++) {
      previous = &out->segments[j];
      if (segment.va < previous->va + previous->memsz && previous->va < segment.va + segment.memsz)
        return -ERR_INVALID;
      if (((previous->prot | segment.prot) & (MAP_EXEC | MAP_WRITE)) == (MAP_EXEC | MAP_WRITE) &&
          ALIGN_DOWN(segment.va, PAGE_SIZE) < ALIGN_UP(previous->va + previous->memsz, PAGE_SIZE) &&
          ALIGN_DOWN(previous->va, PAGE_SIZE) < ALIGN_UP(segment.va + segment.memsz, PAGE_SIZE))
        return -ERR_INVALID;
    }
    if ((segment.prot & MAP_EXEC) && segment.va <= entry && entry - segment.va < segment.memsz)
      executable = 1;
    j = out->count++;
    while (j && out->segments[j - 1].va > segment.va) {
      segmentcopy(&out->segments[j], &out->segments[j - 1]);
      j--;
    }
    segmentcopy(&out->segments[j], &segment);
  }
  if (!out->count || !executable)
    return -ERR_INVALID;
  for (i = 0; i < out->count; i++) {
    begin = ALIGN_DOWN(out->segments[i].va, PAGE_SIZE);
    end = ALIGN_UP(out->segments[i].va + out->segments[i].memsz, PAGE_SIZE);
    if (end > MAX(begin, page_end))
      pages += (end - MAX(begin, page_end)) >> PAGE_SHIFT;
    page_end = MAX(page_end, end);
    if (pages > max_pages)
      return -ERR_LIMIT;
  }
  out->entry = entry;
  out->pages = pages;
  return 0;
}
