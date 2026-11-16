/* Trusted boot windows and user-mapping leases, not physical-address authority. */
#ifndef _DEVICE_H
#define _DEVICE_H
#include <dtb.h>
#include <acct.h>
#define DEVWINDOWS (DT_DEVICES * DT_REGS)
#define DEV_BAD    -1
#define DEV_LIMIT  -2
#define DEV_DENIED -7
typedef struct Device Device;
struct Device
{
  Dtrange actual, pages;
  Acctid  owner;
  u32     refs, mappings, kind, allowed;
};
STATIC_ASSERT(sizeof(Device) == 64, device_window_layout);
int devinit(const Dtinfo *info);
int devwindow(u32 device, u32 region, u32 *out);
int devstat(u32 id, Device *out);
int devhold(u32 id);
int devdrop(u32 id);
int devpage(u32 id, u32 page, u64 *out);
int devmap(u32 id, Acctid acct);
int devunmap(u32 id, Acctid acct);
int devcheck(void);
#endif
