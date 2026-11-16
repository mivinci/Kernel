#ifndef _LIBSYS_START_H
#define _LIBSYS_START_H
#include <sys/boot.h>
/* Runtime adapter: bind only fully checked, immutable startup metadata. */
isize           sysstart(const Boot *boot, u64 available, const Bootview **out);
const Bootview *sysboot(void);
#endif
