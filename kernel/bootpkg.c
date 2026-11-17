#include <bootpkg.h>
#include <bootimage.h>

static u64
read64(const u8 *p)
{
  u64 value = 0;
  u32 i;
  for (i = 0; i < 8; i++)
    value |= (u64) p[i] << (i * 8);
  return value;
}

static int
within(u64 off, u64 bytes, u64 total)
{
  return off <= total && bytes <= total - off;
}

/* All extents are bounds-checked before this helper. */
static int
overlap(u64 a, u64 n, u64 b, u64 m)
{
  return n && m && a < b + m && b < a + n;
}

static void
itemread(const u8 *p, Bootitem *item)
{
  item->name_off = read64(p);
  item->name_bytes = read64(p + 8);
  item->elf_off = read64(p + 16);
  item->elf_bytes = read64(p + 24);
  item->load_off = read64(p + 32);
  item->load_bytes = read64(p + 40);
  item->flags = read64(p + 48);
  item->reserved = read64(p + 56);
}

static int
loadcheck(const u8 *data, u64 bytes, u64 machine)
{
  Bootimage image;
  return !bootimagecheck(data, bytes, &image) &&
         !(image.entry & (machine == BOOTPKG_ARM64 ? 3 : 1));
}

static int
elfcheck(const u8 *data, u64 bytes, u64 machine)
{
  return bytes >= 64 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' &&
         data[4] == 2 && data[5] == 1 && data[6] == 1 && data[16] == 2 && !data[17] &&
         ((u64) data[18] | ((u64) data[19] << 8)) == machine && data[20] == 1 && !data[21] &&
         !data[22] && !data[23];
}

static int
same(const u8 *a, const u8 *b, u64 bytes)
{
  while (bytes--)
    if (*a++ != *b++)
      return 0;
  return 1;
}

static int
itemoverlap(const Bootitem *a, const Bootitem *b)
{
  const u64 offa[3] = {a->name_off, a->elf_off, a->load_off};
  const u64 sizea[3] = {a->name_bytes, a->elf_bytes, a->load_bytes};
  const u64 offb[3] = {b->name_off, b->elf_off, b->load_off};
  const u64 sizeb[3] = {b->name_bytes, b->elf_bytes, b->load_bytes};
  u32       i, j;
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
      if (overlap(offa[i], sizea[i], offb[j], sizeb[j]))
        return 1;
  return 0;
}

int
bootpkgcheck(const void *input, u64 bytes, u64 machine, Bootdir *out)
{
  const u8 *data = input;
  u8       *zero = (u8 *) out;
  u64       count, table, init, checksum = UINT64_C(0xcbf29ce484222325), i, j;
  Bootitem  item, previous;
  if (!out)
    return BOOTPKG_HEADER;
  for (i = 0; i < sizeof(*out); i++)
    zero[i] = 0;
  if (!data || bytes < sizeof(Bootpkg) || bytes > ~(u64) 0 - (u64) data ||
      (machine != BOOTPKG_ARM64 && machine != BOOTPKG_RISCV) || read64(data) != BOOTPKG_MAGIC ||
      read64(data + 8) != BOOTPKG_FORMAT || read64(data + 16) != bytes ||
      read64(data + 24) != machine)
    return BOOTPKG_HEADER;
  count = read64(data + 32);
  table = read64(data + 40);
  init = read64(data + 48);
  if (!count || count > BOOTPKG_ITEMS || init >= count || table < sizeof(Bootpkg) || (table & 7) ||
      !within(table, count * sizeof(Bootitem), bytes))
    return BOOTPKG_DIRECTORY;
  for (i = 0; i < bytes; i++)
    if (i < 56 || i >= 64)
      checksum = (checksum ^ data[i]) * UINT64_C(0x100000001b3);
  if (checksum != read64(data + 56))
    return BOOTPKG_CHECKSUM;
  for (i = 0; i < count; i++) {
    itemread(data + table + i * sizeof(Bootitem), &item);
    if (item.flags || item.reserved || !within(item.name_off, item.name_bytes, bytes) ||
        !within(item.elf_off, item.elf_bytes, bytes) ||
        !within(item.load_off, item.load_bytes, bytes))
      return BOOTPKG_DIRECTORY;
    if (!item.name_bytes || item.name_bytes > BOOTPKG_NAME_BYTES)
      return BOOTPKG_NAME;
    for (j = 0; j < item.name_bytes; j++)
      if (!data[item.name_off + j])
        return BOOTPKG_NAME;
    if (!elfcheck(data + item.elf_off, item.elf_bytes, machine))
      return BOOTPKG_ELF;
    if (i == init) {
      if (!item.load_bytes || (item.load_off & 7) ||
          !loadcheck(data + item.load_off, item.load_bytes, machine))
        return BOOTPKG_LOAD;
    } else if (item.load_off || item.load_bytes)
      return BOOTPKG_LOAD;
    {
      const u64 off[3] = {item.name_off, item.elf_off, item.load_off};
      const u64 size[3] = {item.name_bytes, item.elf_bytes, item.load_bytes};
      u32       a, b;
      for (a = 0; a < 3; a++) {
        if (overlap(off[a], size[a], 0, sizeof(Bootpkg)) ||
            overlap(off[a], size[a], table, count * sizeof(Bootitem)))
          return BOOTPKG_OVERLAP;
        for (b = 0; b < a; b++)
          if (overlap(off[a], size[a], off[b], size[b]))
            return BOOTPKG_OVERLAP;
      }
    }
    for (j = 0; j < i; j++) {
      itemread(data + table + j * sizeof(Bootitem), &previous);
      if (item.name_bytes == previous.name_bytes &&
          same(data + item.name_off, data + previous.name_off, item.name_bytes))
        return BOOTPKG_NAME;
      if (itemoverlap(&item, &previous))
        return BOOTPKG_OVERLAP;
    }
  }
  /* The complete input is valid; only now publish the directory. */
  out->header.magic = BOOTPKG_MAGIC;
  out->header.format = BOOTPKG_FORMAT;
  out->header.bytes = bytes;
  out->header.machine = machine;
  out->header.count = count;
  out->header.entries_off = table;
  out->header.init_index = init;
  out->header.checksum = checksum;
  for (i = 0; i < count; i++)
    itemread(data + table + i * sizeof(Bootitem), &out->item[i]);
  return 0;
}
