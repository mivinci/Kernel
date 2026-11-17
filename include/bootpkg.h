#ifndef _BOOTPKG_H
#define _BOOTPKG_H
#include <abi/bootpkg.h>
typedef struct Bootdir Bootdir;
struct Bootdir
{
  Bootpkg  header;
  Bootitem item[BOOTPKG_ITEMS];
};
STATIC_ASSERT(sizeof(Bootdir) == 64 + BOOTPKG_ITEMS * 64, boot_directory);
enum Booterror
{
  BOOTPKG_HEADER = -1,
  BOOTPKG_CHECKSUM = -2,
  BOOTPKG_DIRECTORY = -3,
  BOOTPKG_NAME = -4,
  BOOTPKG_ELF = -5,
  BOOTPKG_LOAD = -6,
  BOOTPKG_OVERLAP = -7
};
/* Immutable boot input; no alignment requirement, heap or partial publication.
   out must be disjoint from data. On failure its entire value is zero. */
int bootpkgcheck(const void *data, u64 bytes, u64 machine, Bootdir *out);
#endif
