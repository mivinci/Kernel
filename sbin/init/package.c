#include "package.h"

static u64
read64(const u8 *p)
{
  u64 value = 0;
  u32 i;
  for (i = 0; i < 8; i++)
    value |= (u64) p[i] << (i * 8);
  return value;
}
static void
clear(Package *package)
{
  u8 *p = (u8 *) package;
  u64 i;
  for (i = 0; i < sizeof(*package); i++)
    p[i] = 0;
}
static int
within(u64 off, u64 bytes, u64 total)
{
  return off <= total && bytes <= total - off;
}
static int
overlap(u64 a, u64 n, u64 b, u64 m)
{
  return n && m && a < b + m && b < a + n;
}
static u64
offset(const Bootitem *item, u32 index)
{
  return index == 0 ? item->name_off : index == 1 ? item->elf_off : item->load_off;
}
static u64
length(const Bootitem *item, u32 index)
{
  return index == 0 ? item->name_bytes : index == 1 ? item->elf_bytes : item->load_bytes;
}
static int
same(const u8 *a, const u8 *b, u64 bytes)
{
  while (bytes--)
    if (*a++ != *b++)
      return 0;
  return 1;
}
int
packageopen(const void *input, u64 bytes, u64 machine, Package *out)
{
  const u8 *data = input, *elf, *row;
  Bootpkg  *header;
  Bootitem *item;
  u64       digest = UINT64_C(0xcbf29ce484222325), i, j;
  u32       a, b;
  if (!out)
    return -1;
  clear(out);
  if (!data || bytes < sizeof(Bootpkg) || bytes > ~(u64) 0 - (u64) data ||
      (machine != BOOTPKG_ARM64 && machine != BOOTPKG_RISCV))
    return -1;
  header = &out->header;
  header->magic = read64(data);
  header->format = read64(data + 8);
  header->bytes = read64(data + 16);
  header->machine = read64(data + 24);
  header->count = read64(data + 32);
  header->entries_off = read64(data + 40);
  header->init_index = read64(data + 48);
  header->checksum = read64(data + 56);
  if (header->magic != BOOTPKG_MAGIC || header->format != BOOTPKG_FORMAT ||
      header->bytes != bytes || header->machine != machine || !header->count ||
      header->count > BOOTPKG_ITEMS || header->init_index >= header->count ||
      header->entries_off < sizeof(Bootpkg) || (header->entries_off & 7) ||
      !within(header->entries_off, header->count * sizeof(Bootitem), bytes))
    goto bad;
  for (i = 0; i < bytes; i++)
    if (i < 56 || i >= 64)
      digest = (digest ^ data[i]) * UINT64_C(0x100000001b3);
  if (digest != header->checksum)
    goto bad;
  for (i = 0; i < header->count; i++) {
    item = &out->files[i];
    row = data + header->entries_off + i * sizeof(Bootitem);
    item->name_off = read64(row);
    item->name_bytes = read64(row + 8);
    item->elf_off = read64(row + 16);
    item->elf_bytes = read64(row + 24);
    item->load_off = read64(row + 32);
    item->load_bytes = read64(row + 40);
    item->flags = read64(row + 48);
    item->reserved = read64(row + 56);
    if (item->flags || item->reserved || !item->name_bytes ||
        item->name_bytes > BOOTPKG_NAME_BYTES || item->elf_bytes < 64 ||
        !within(item->name_off, item->name_bytes, bytes) ||
        !within(item->elf_off, item->elf_bytes, bytes) ||
        !within(item->load_off, item->load_bytes, bytes) ||
        (i == header->init_index ? !item->load_bytes || (item->load_off & 7)
                                 : item->load_off || item->load_bytes))
      goto bad;
    for (j = 0; j < item->name_bytes; j++)
      if (!data[item->name_off + j])
        goto bad;
    elf = data + item->elf_off;
    if (elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F' || elf[4] != 2 ||
        elf[5] != 1 || elf[6] != 1 || read64(elf + 16) != (2UL | (machine << 16) | (1UL << 32)))
      goto bad;
    for (a = 0; a < 3; a++) {
      u64 off = offset(item, a), size = length(item, a);
      if (overlap(off, size, 0, sizeof(Bootpkg)) ||
          overlap(off, size, header->entries_off, header->count * sizeof(Bootitem)))
        goto bad;
      for (b = 0; b < a; b++)
        if (overlap(off, size, offset(item, b), length(item, b)))
          goto bad;
      for (j = 0; j < i; j++)
        for (b = 0; b < 3; b++)
          if (overlap(off, size, offset(&out->files[j], b), length(&out->files[j], b)))
            goto bad;
    }
    for (j = 0; j < i; j++)
      if (item->name_bytes == out->files[j].name_bytes &&
          same(data + item->name_off, data + out->files[j].name_off, item->name_bytes))
        goto bad;
  }
  out->data = data;
  return 0;
bad:
  clear(out);
  return -1;
}

const Bootitem *
packagefind(const Package *package, const char *name)
{
  u64 bytes = 0, i;
  if (!package || !package->data || !name)
    return NULL;
  while (bytes <= BOOTPKG_NAME_BYTES && name[bytes])
    bytes++;
  if (!bytes || bytes > BOOTPKG_NAME_BYTES)
    return NULL;
  for (i = 0; i < package->header.count; i++)
    if (package->files[i].name_bytes == bytes &&
        same(package->data + package->files[i].name_off, (const u8 *) name, bytes))
      return &package->files[i];
  return NULL;
}
