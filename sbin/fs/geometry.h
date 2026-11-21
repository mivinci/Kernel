#ifndef _FS_GEOMETRY_H
#define _FS_GEOMETRY_H
#include <types.h>
#include <abi/base.h>

#define FS_BLOCK_BYTES 4096
#define FS_DATA_START  99
#define FS_MAX_BLOCKS  32768

typedef struct Fsgeometry Fsgeometry;
struct Fsgeometry
{
  u64 bytes;
  u32 logical, physical, alignment;
  u32 readonly, flush;
  /* Trusted storage contract. Zero means no verified fault boundary. */
  u32 faultbytes, faultoffset;
};

/* The volume starts at device byte zero. Check a decoded volume size
   against this device snapshot. Set write for both redo and writable
   mounts. The caller separately holds exclusive access for either writer. */
isize fsgeometry(const Fsgeometry *device, u64 blocks, u32 write);
#endif
