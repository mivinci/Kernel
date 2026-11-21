#ifndef _DMA_H
#define _DMA_H
#include <mem.h>

#define DMA_MAX_PAGES 1024
typedef struct Dmaid Dmaid;
struct Dmaid
{
  u64 generation;
  u32 device, reserved;
};

/* Trusted kernel entry points. The caller serializes operations and checks
   device policy. A device index names the physical DT device, not an MMIO
   page group. Data pages stay charged to the original account. */
int dmaopen(u32 device, Acctid acct, Dmaid *out);
int dmaallocpage(Dmaid id, Memid *out);
/* Call after device stop and required DMA synchronization are confirmed.
   Positive means more pins remain; continue with the same generation. */
int dmaend(Dmaid id);
#endif
