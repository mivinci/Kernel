#ifndef _LIBSYS_SERIAL_H
#define _LIBSYS_SERIAL_H
#include <proto/serial.h>
#include <abi/base.h>

/* Both mappings are ordinary coherent SHARED memory, never MMIO/DMA.
   Initialize once before sharing. Reinitialization requires both peers stopped;
   this helper does not implement the live epoch-reset handshake. */
isize serialinit(Serialring *ring, u64 epoch);
/* Exactly one producer and one consumer. Buffers are private, valid and disjoint
   from the entire ring. Each call moves <=32 bytes, returns the byte count or a
   negative native status. Zero means no progress, not EOF. Persistent flags and
   positions, not notifications, determine when the caller may sleep.
   Epoch/geometry stay immutable while peers run. No helper silently wraps an
   index, clears flags, yields, notifies or resets a live ring. */
isize serialwrite(Serialring *ring, u64 epoch, const void *data, u32 bytes);
isize serialread(Serialring *ring, u64 epoch, void *data, u32 bytes);
/* Inspect the next consumer byte without publishing any room to the producer. */
isize serialpeek(Serialring *ring, u64 epoch, u8 *byte);
#endif
