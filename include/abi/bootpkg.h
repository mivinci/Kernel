/* Boot container wire format, distinct from process Boot and Bootimage.
   All fields are little endian u64; decode bytes before using unaligned input.
   Directory/load offsets are multiples of 8, not necessarily PAGE_SIZE.
   Names are opaque non-NUL bytes with explicit lengths. Flags/reserved are 0. */
#ifndef _ABI_BOOTPKG_H
#define _ABI_BOOTPKG_H
#include <types.h>
#define BOOTPKG_MAGIC      UINT64_C(0x4b504b4730303031)
#define BOOTPKG_FORMAT     1
#define BOOTPKG_ITEMS      32
#define BOOTPKG_NAME_BYTES 64
#define BOOTPKG_ARM64      183
#define BOOTPKG_RISCV      243
typedef struct Bootpkg  Bootpkg;
typedef struct Bootitem Bootitem;
struct Bootpkg
{
  u64 magic, format, bytes, machine, count, entries_off, init_index, checksum;
};
struct Bootitem
{
  u64 name_off, name_bytes, elf_off, elf_bytes, load_off, load_bytes, flags, reserved;
};
STATIC_ASSERT(sizeof(Bootpkg) == 64, boot_package_header);
STATIC_ASSERT(sizeof(Bootitem) == 64, boot_package_entry);
STATIC_ASSERT(offsetof(Bootpkg, checksum) == 56, boot_package_checksum);
STATIC_ASSERT(offsetof(Bootitem, load_off) == 32, boot_package_descriptor);
#endif
