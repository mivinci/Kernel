#ifndef _BLK_TRANSPORT_H
#define _BLK_TRANSPORT_H
#include "io.h"
#include "private/filewatch.h"
#include <proto/blk.h>
#include <sys/channel.h>

typedef struct Blkjob       Blkjob;
typedef struct Blktransport Blktransport;
typedef isize (*Blkdrainlink)(void *context, Filelink *link);
typedef isize (*Blkfilefinish)(Fileinflight *active, Handle request);
struct Blkjob
{
  Handle request;
  u64    seq, offset;
  u32    length, type, protocol_slot, queue_slot, link;
  u64    status, value, bytes;
  u32    completed, replied, slotdone;
};
struct Blktransport
{
  Blkqueue     *queue;
  Filelink     *links;
  Blkjob       *jobs;
  Blkjob        immediate;
  Vblksync      sync;
  void         *syncctx;
  u32           nlinks, njobs;
  void         *reaper;
  Blkdrainlink  drain;
  Blkfilefinish filefinish;
  u64           device;
  u64           error;
  u32           state, active;
};

/* Fresh zeroed transport storage. One queue slot is reserved per private job;
   the caller supplies all fixed storage, the queue's real DMA sync callback,
   and retains device/connection setup. */
isize blktransportinit(Blktransport *transport, Blkqueue *queue, Filelink *links, u32 nlinks,
                       Blkjob *jobs, u32 njobs, u64 device, Vblksync sync, void *syncctx);
void  blktransportfilefinish(Blktransport *transport, Blkfilefinish finish);
isize blktransportreaper(Blktransport *transport, void *context, Blkdrainlink drain);
/* Admit one INFO/READ/WRITE/FLUSH CALL using the common protected slot ledger.
   If an earlier immediate completion still needs retry, reject the new CALL
   with EAGAIN before claiming its slot. HELLO uses sysslothello separately. */
isize blktransportcall(Blktransport *transport, const Ipcmsg *call);
/* Retry immediate and device completions, including file-ledger release. */
isize blktransportpump(Blktransport *transport);
/* Consume one completion accepted by this queue. READ is copied only after a
   successful device completion and DMA AFTER; cancellation degrades to discard. */
isize blktransportfinish(Blktransport *transport, const Vdone *done);
/* Control and recovery paths finish an already-drained job without pretending
   that a device result was known. An in-flight job keeps its recorded error. */
isize blktransportabort(Blktransport *transport, Blkjob *job, isize status);
/* Poll one bounded batch, complete each request, then expose IRQ causes for ACK. */
isize blktransportpoll(Blktransport *transport, Vdone *done, u32 *count, u32 *more, u32 *causes);
/* Once a queue or completion fault is observed, refuse new I/O and report EIO
   to every live request. Private DMA storage remains quarantined in Blkqueue. */
isize blktransportfail(Blktransport *transport, isize error);
/* Advance reset/reclaim without admitting new I/O. Zero means old windows are
   safe and the caller may bind a newer queue generation. */
isize blktransportrecover(Blktransport *transport, u64 now);
/* Sweep ended clients only after their block jobs have drained. */
isize blktransportreap(Blktransport *transport);
int   blktransportbusy(const Blktransport *transport, const Filelink *link);
u64   blktransportgeneration(const Blktransport *transport);
#endif
