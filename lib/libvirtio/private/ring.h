/* Internal modern split-ring transport. Buffers remain the caller's responsibility. */
#ifndef _VIRTIO_RING_H
#define _VIRTIO_RING_H
#include "queue.h"
#define VR_DESC    0
#define VR_AVAIL   1
#define VR_USED    2
#define VR_BEFORE  0
#define VR_AFTER   1
#define VR_RELEASE 0
#define VR_ACQUIRE 1

typedef struct Vdesc     Vdesc;
typedef struct Vavail    Vavail;
typedef struct Vusedring Vusedring;
typedef struct Vbuf      Vbuf;
typedef struct Vringops  Vringops;
typedef struct Vring     Vring;
struct Vdesc
{
  u64 address;
  u32 length;
  u16 flags, next;
};
struct Vavail
{
  u16 flags, index, ring[VQ_SIZE];
};
struct Vusedring
{
  u16   flags, index;
  Vused ring[VQ_SIZE];
};
struct Vbuf
{
  u64 address;
  u32 length, writable;
};
struct Vringops
{
  void *context;
  /* Each region is one exclusive DMA page. DESC/AVAIL are device-read,
     USED is device-write. Return 0 or a negative status, never wait for I/O. */
  int (*sync)(void *context, u32 region, u32 phase, u64 offset, u64 length);
  void (*fence)(void *context, u32 phase);
  int (*notify)(void *context);
};
struct Vring
{
  Vq                  queue;
  volatile Vdesc     *desc;
  volatile Vavail    *avail;
  volatile Vusedring *used;
  Vringops            ops;
  u16                 nextavail, nextused;
  u32                 initialized, backlog;
};
STATIC_ASSERT(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, virtio_little_endian_profile);
STATIC_ASSERT(sizeof(Vdesc) == 16, virtio_descriptor_layout);
STATIC_ASSERT(sizeof(Vavail) == 260, virtio_available_layout);
STATIC_ASSERT(sizeof(Vusedring) == 1028, virtio_used_layout);
/* Three distinct aligned/exclusively owned DMA pages; device already stopped.
   Does not enable hardware or prove that a previous device instance stopped. */
int vrinit(Vring *ring, u64 generation, void *desc, void *avail, void *used, const Vringops *ops);
/* After the owner confirms device stop, return all ring pages to the CPU.
   A retry resumes at the first page whose AFTER did not succeed. */
int vrreclaim(Vring *ring, u32 *next);
/* Caller validated all device addresses/domain, completed data BEFORE, and
   retains buffers. published=1 means avail.idx may be visible, even on error.
   Before publication caller must AFTER any prepared buffers before reusing.
   Broken queues require recovery; no operation here unpins DMA memory. */
int vrsubmit(Vring *ring, const Vbuf *buffers, u32 count, u64 cookie, u32 *published);
/* Single-threaded, immutable/disjoint caller storage. Returns at most 32 done
   requests; caller performs data AFTER before delivery or new submissions.
   more reflects the observed used.idx only, not permission to sleep with
   notifications suppressed. This layer always keeps avail.flags=0. */
int vrpoll(Vring *ring, u64 generation, Vdone *out, u32 *count, u32 *more);
#endif
