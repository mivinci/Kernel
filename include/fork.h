/* Trusted synchronous fork snapshot; native admission/publication is separate. */
#ifndef _FORK_H
#define _FORK_H
#include <local.h>
struct Hctx;
/* Source is a frozen ACTIVE parent, target its fresh PREPARING child with an
   empty Local table/Space and reserved signal bank. Limits are its actual
   Acct/table/mail specification. IRQs masked, both roots inactive, no checkpoint.
   On success target owns Space/Local/Signal/user state and receive floors;
   parent keeps its user state and data. On failure maps/references/new parent
   protections/adoptions roll back and output Hctx stays unchanged. Target must
   be cancelled after failure: cookies may be burned, no bank reuse promised.
   With nonzero prep, the exact active FORK/CLONING record owns a second
   root lease. A zero Prepid selects the standalone trusted mechanism. Caller
   ends that clone phase after success/failure. This does not create a Proc,
   grant authority, install Fix, or publish/run it. */
int forksnapshot(Rtable *source, Rtable *target, const Createarg *limits, u32 reserve,
                 const struct Hctx *parent, struct Hctx *child, Prepid prep);
/* A checkpoint runs between preparation batches. It returns with IRQs masked
   and both roots inactive, or hands off the Prep. */
int forksnapshotchecked(Rtable *source, Rtable *target, const Createarg *limits, u32 reserve,
                        const struct Hctx *parent, struct Hctx *child, Prepid prep,
                        Forkcheck checkpoint);
#endif
