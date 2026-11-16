#ifndef _SYS_BOOT_H
#define _SYS_BOOT_H
#include <abi/boot.h>
typedef struct Bootview Bootview;
struct Bootview
{
  const Boot     *header;
  const Strref   *argv, *env;
  const Resentry *resources;
  const Bootfd   *fds;
};
/* data must remain readable and unchanged for the supplied byte count. This
   checks the representation; a resource tag cannot create kernel authority. */
isize       bootcheck(const void *data, u64 available, Bootview *out);
Handle      bootresource(const Bootview *view, u64 tag);
const char *bootarg(const Bootview *view, u64 index, u32 environment);
#endif
