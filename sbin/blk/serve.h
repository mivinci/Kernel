#ifndef _BLK_SERVE_H
#define _BLK_SERVE_H
#include "transport.h"

typedef struct Blkservice Blkservice;
struct Blkservice
{
  Blktransport *transport;
  Handle        observe, allocator, space, irq;
  u64           arena, irqevent;
  u32           pending, retiring;
};

/* The owner supplies a live transport with a reaper, a dedicated IRQ bit,
   an armed binding and a private connection arena. On error the owner keeps
   the transport and DMA storage so it can retry or run device recovery. */
isize blkservestep(Blkservice *);
isize blkservicecall(Blkservice *, const Ipcmsg *);
/* Stop this instance without rearming its IRQ. Retry with monotonic time
   until reset, DMA handback and original requests have all settled. */
isize blkservestop(Blkservice *, u64 now);
#endif
