/* Boot-only, bounded discovery. These records are internal, not an ABI. */
#ifndef _DTB_H
#define _DTB_H
#include <types.h>
#define DT_RAM      8
#define DT_RESERVED 32
#define DT_DEVICES  64
#define DT_USABLE   64
#define DT_REGS     4
#define DT_IRQS     16
#define DT_CPUS     16

enum Dtkind
{
  DT_UART16550 = 1,
  DT_PL011,
  DT_VIRTIO,
  DT_PLIC,
  DT_GIC,
  DT_CLINT,
  DT_CPUINTC,
  DT_TIMER
};
enum Dterror
{
  DT_BAD = -1,
  DT_FULL = -2,
  DT_UNSUPPORTED = -3,
  DT_CONFLICT = -4
};

typedef struct Dtrange Dtrange;
typedef struct Dtirq   Dtirq;
typedef struct Dtdev   Dtdev;
typedef struct Dtinfo  Dtinfo;
typedef struct Dtuart  Dtuart;

struct Dtrange
{
  u64 base, size;
};
struct Dtuart
{
  Dtrange reg;
  u32     kind, shift, width, reserved;
};
struct Dtirq
{
  u32 parent, number, flags, reserved;
};
struct Dtdev
{
  u32     kind, nreg, nirq, phandle;
  Dtrange reg[DT_REGS];
  Dtirq   irq[DT_IRQS];
  u32     limit, protected, mappable, groupable;
};
struct Dtinfo
{
  u32     size, nram, nreserved, ndev, nusable, cpus;
  u64     timebase;
  Dtrange ram[DT_RAM], reserved[DT_RESERVED], usable[DT_USABLE];
  Dtdev   dev[DT_DEVICES];
  Dtrange initrd;
  Dtrange bootedge[2];
};

/* Single boot caller: parser scratch is static, no heap or recursion. */
int dtread(const void *blob, usize available, Dtinfo *info);
/* Cold preparser; scratch reuses the caller's later discovery buffer. */
int dtconsole(const void *blob, usize available, Dtinfo *scratch, Dtuart *out);
/* Check entire touched pages against boot/firmware ownership before any read. */
int dtinitrdcheck(const Dtinfo *info, u64 kernel, u64 bytes, u64 dtb);
int dtreserve(Dtinfo *info, u64 base, u64 size);
int dtusable(Dtinfo *info);
#endif
