#ifndef _BOOTSTORE_H
#define _BOOTSTORE_H
#include <dtb.h>
typedef struct Bootstore Bootstore;
struct Bootstore
{
  u64 base, bytes, pages, copies, retained, refs;
};
enum Bootstoreerror
{
  BOOTSTORE_BAD = -1,
  BOOTSTORE_LIMIT = -2,
  BOOTSTORE_FULL = -3,
  BOOTSTORE_NOMEM = -4,
  BOOTSTORE_BUSY = -5
};
/* Single trusted boot caller, after full container validation and boot
   reservations, before pginit. No partial Dtinfo/store on failure. */
int bootstoreprepare(Dtinfo *info);
/* Read-only physical backing, never a userspace PA mapping interface. */
int bootstorepage(u64 index, u64 *pa);
int bootstorestat(Bootstore *out);
int bootstorehold(void);
int bootstoredrop(void);
#endif
