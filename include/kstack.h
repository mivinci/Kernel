/* Trusted kernel stack ownership; never a userspace Mem or Handle. */
#ifndef _KSTACK_H
#define _KSTACK_H
#include <acct.h>
#include <hal/vm.h>
#include <layout.inc>
#ifndef KSTACKS
#define KSTACKS 64
#endif
#define KSTACK_PAGES 4
#define KS_BAD       -1
#define KS_FULL      -3
#define KS_BUSY      -4
/* Private 2 MiB slots keep each leaf table charged to exactly one account.
   The permanent KIO parent is shared by every user root. */

typedef struct Kstackid Kstackid;
typedef struct Kstack   Kstack;
struct Kstackid
{
  u64 generation;
  u32 index, reserved;
};
struct Kstack
{
  u64    generation;
  Acctid acct;
  u64    pa[KSTACK_PAGES], table;
  u32    refs, state;
};
STATIC_ASSERT(KSTACKS > 0 && KSTACKS <= 64, kstack_capacity);
STATIC_ASSERT(sizeof(Kstack) <= 80, kstack_record);
int kstackinit(Hvs *kernel);
int kstackcreate(Acctid acct, Kstackid *out);
/* Each resumable continuation owns a hold until explicitly discarded off-stack. */
int kstackhold(Kstackid id);
int kstackdrop(Kstackid id);
int kstackfree(Kstackid id);
int kstackstat(Kstackid id, Kstack *out);
int kstacktop(Kstackid id, u64 *out);
/* Off-stack watermark includes setup and observed execution, not a depth proof. */
int kstackused(Kstackid id, u64 *out);
#endif
