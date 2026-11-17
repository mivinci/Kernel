/* CPU-private split-queue ownership; never place this structure in DMA RAM. */
#ifndef _VIRTIO_QUEUE_H
#define _VIRTIO_QUEUE_H
#include <types.h>
#include <abi/base.h>
#define VQ_SIZE      128
#define VQ_BATCH     32
#define VQ_NONE      128
#define VQ_FREE      0
#define VQ_RESERVED  1
#define VQ_PUBLISHED 2
#define VQ_MEMBER    3

typedef struct Vslot Vslot;
typedef struct Vq    Vq;
typedef struct Vused Vused;
typedef struct Vdone Vdone;
struct Vslot
{
  u64 cookie;
  u32 writable;
  u16 next, state;
};
struct Vq
{
  Vslot slots[VQ_SIZE];
  u64   generation;
  u32   free, head, broken, reserved;
};
struct Vused
{
  u32 id, length;
};
struct Vdone
{
  u64 cookie;
  u32 length, reserved;
};
STATIC_ASSERT(sizeof(Vslot) == 16, vslot_layout);
/* Initialize fresh CPU storage only. Reinitialization does not stop DMA. */
int vqinit(Vq *queue, u64 generation);
/* Reserve an unpublished chain. writable is total device-writable capacity. */
int vqreserve(Vq *queue, u32 count, u32 writable, u64 cookie, u32 *head);
/* Adapter copies the private chain into DMA descriptors, syncs, then marks it
   published before exposing avail.idx. No concurrent/reentrant queue access. */
int vqpublish(Vq *queue, u32 head);
int vqcancel(Vq *queue, u32 head);
/* Adapter snapshots used entries after DMA acquire, at most VQ_BATCH. Entire
   batch is validated before any chain is freed; invalid input poisons queue.
   Generation belongs to the adapter session, not a field on the wire. Inputs
   are an immutable CPU snapshot; used/out/queue storage must be disjoint.
   Completion frees descriptor slots only, never DMA pins or buffer ownership. */
int vqcomplete(Vq *queue, u64 generation, const Vused *used, u32 count, Vdone *out);
#endif
