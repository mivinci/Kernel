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
/* Trusted identity-DMA platforms only. limits describes the physical
   aperture and allocation alignment. coherency is a platform property. */
int dmaopen(u32 device, Acctid acct, const Pglim *limits, u32 coherent, Dmaid *out);
/* The request can narrow the domain aperture and increase alignment.
   Charge the entire contiguous buddy block; expose only the requested
   whole pages. A null request uses the domain's allocation limits. */
int dmaalloc(Dmaid id, u64 bytes, const Pglim *request, Memid *out);
int dmaaddress(Dmaid id, Memid memory, u64 offset, u64 bytes, u64 *out);
/* The caller owns the full maintenance granules touched by the window.
   Recovery may synchronize AFTER even if the last manager has exited. */
int dmasync(Dmaid id, Memid memory, u64 offset, u64 bytes, u32 phase, u32 direction);
/* After confirmed stop and exclusion of CPU access, synchronize all pins
   without needing their former manager's handles. Start with a zero cursor.
   Positive means resume with that cursor; zero means all pins synchronized.
   Keep the pin list unchanged through the traversal. No pins are released. */
int dmaafter(Dmaid id, Memid *cursor);
/* Call after device stop and required DMA synchronization are confirmed.
   Positive means more pins remain; continue with the same generation. */
int dmaend(Dmaid id);
#endif
