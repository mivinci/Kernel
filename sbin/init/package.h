#ifndef _INIT_PACKAGE_H
#define _INIT_PACKAGE_H
#include <abi/bootpkg.h>
typedef struct Package Package;
struct Package
{
  const u8 *data;
  Bootpkg   header;
  Bootitem  files[BOOTPKG_ITEMS];
};
/* Immutable kernel-granted view. Check directory/content references here;
   kernel-only init load-descriptor internals are not interpreted by init. */
int             packageopen(const void *data, u64 bytes, u64 machine, Package *out);
const Bootitem *packagefind(const Package *package, const char *name);
#endif
