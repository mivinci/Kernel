#ifndef _FS_BLK_H
#define _FS_BLK_H
#include <proto/blk.h>
#include <sys/channel.h>
#include "cache.h"
#include "private/bootstrap.h"

typedef struct Fsblk     Fsblk;
typedef struct Blkattach Blkattach;
typedef struct Blkfetch  Blkfetch;
struct Readjob;
struct Readfetch;
struct Loadjob;
struct Loadfetch;
struct Journal;
typedef isize (*Blkfetchdone)(void *, Handle, const Cacheio *, isize, const void *);
struct Blkattach
{
  Bootstrap boot;
  Bootmap   map;
  Handle    call, space, memory;
  u64       address, peerimage, device;
};
struct Fsblk
{
  Handle  call, rpc;
  u64     address, connid, device, seq;
  Blkinfo info, nextinfo;
  isize   result;
  u32     opcode, bytes, saved, stale, ended;
  u8      data[BLK_MAX_IO];
};

/* Start a single HELLO on the fixed CALL. Use zero-initialized records;
   the mount owner reserves the VA and slot zero, keeps the CALL alive,
   and serializes all operations. Poll returns zero with either a bound
   Fsblk or a validated negative business status. */
isize fsblkhello(Blkattach *, Handle call, Handle space, u64 address, u64 device, u64 seq);
isize fsblkattached(Blkattach *, Fsblk *, isize *status);
int   fsblkbound(const Blkattach *, const Fsblk *);
/* Settle the original bootstrap and release the local mapping and MEM.
   The owner must first stop using Fsblk and drain its I/O. This does not
   close the server's peer entry; it stays owned by the client image. On
   success both local records are reset for use with a new connection. */
isize fsblkdetach(Blkattach *, Fsblk *);
/* On success one original RPC is owned. A failure leaves no submitted I/O.
   WRITE input is copied before submission and is independent of the mapping. */
isize fsblksubmit(Fsblk *, u32 opcode, u64 offset, u32 bytes, const void *write);
/* Nonblocking. Returns zero with the fixed business status after real FINISH,
   validation and ACK. READ needs private output for the requested byte count.
   INFO installs the verified geometry in info. On error the RPC and slot
   remain owned for retry, and output is untouched. */
isize fsblkpoll(Fsblk *, void *read, u32 capacity, isize *status);

struct Blkfetch
{
  Fsblk                *blk;
  const struct Journal *journal;
  Blkfetchdone          done;
  void                 *job;
  Handle                request, rpc;
  Cacheio               ticket;
  u64                   sequence;
  isize                 status;
  u32                   collected;
  u8                    data[CACHE_BYTES];
};

/* One lower read at a time. The caller keeps the upper job and cache ticket
   alive; done receives the fixed result only after lower FINISH and ACK. */
isize blkfetchstart(Blkfetch *, void *job, Handle request, const Cacheio *, u32 block,
                    Blkfetchdone);
isize blkfetchpoll(Blkfetch *);
/* Submit callbacks for a file service whose backend context is Blkfetch. */
isize blkfetchread(void *, struct Readjob *, const struct Readfetch *);
isize blkfetchload(void *, struct Loadjob *, const struct Loadfetch *);
#endif
