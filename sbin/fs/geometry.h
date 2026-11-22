#ifndef _FS_GEOMETRY_H
#define _FS_GEOMETRY_H
#include <types.h>
#include <abi/base.h>

#define FS_BLOCK_BYTES 4096
#define FS_FILE_BYTES  (4 * 1024 * 1024)
#define FS_DATA_START  99
#define FS_MAX_BLOCKS  32768
#define FS_FORMAT      1
#define FS_INODES      1024
#define FS_INODE_BYTES 128
#define FS_ROOT        1
#define FS_LOG_START   1
#define FS_LOG_BLOCKS  64
#define FS_INODE_MAP   (FS_LOG_START + FS_LOG_BLOCKS)
#define FS_BLOCK_MAP   (FS_INODE_MAP + 1)
#define FS_INODE_START (FS_BLOCK_MAP + 1)

typedef struct Fsgeometry Fsgeometry;
typedef struct Fslayout   Fslayout;
/* Decoded fields only; this type does not describe a disk encoding. */
struct Fslayout
{
  u64 blocks, logstart, logblocks, inodemap, blockmap, inodestart, datastart;
  u32 format, blockbytes, inodes, root;
};
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
/* Verify the fixed layout after the caller authenticates and decodes it. */
isize fslayoutcheck(const Fslayout *layout, const Fsgeometry *device, u32 write);
#endif
