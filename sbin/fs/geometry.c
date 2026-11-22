#include "geometry.h"

isize
fsgeometry(const Fsgeometry *device, u64 blocks, u32 write)
{
  if (!device || write > 1 || device->readonly > 1 || device->flush > 1)
    return -ERR_INVALID;
  if (blocks <= FS_DATA_START || blocks > FS_MAX_BLOCKS || blocks > device->bytes / FS_BLOCK_BYTES)
    return -ERR_STATE;
  if (!device->logical || FS_BLOCK_BYTES % device->logical)
    return -ERR_NOTSUP;
  if ((!device->physical && device->alignment) ||
      (device->physical &&
       (device->physical % device->logical || device->alignment >= device->physical ||
        device->alignment % device->logical)))
    return -ERR_STATE;
  if (write && (device->readonly || !device->flush || !device->faultbytes || device->faultoffset ||
                FS_BLOCK_BYTES % device->faultbytes ||
                (device->physical && (FS_BLOCK_BYTES % device->physical || device->alignment))))
    return -ERR_NOTSUP;
  return 0;
}

isize
fslayoutcheck(const Fslayout *layout, const Fsgeometry *device, u32 write)
{
  if (!layout)
    return -ERR_INVALID;
  if (layout->format != FS_FORMAT || layout->blockbytes != FS_BLOCK_BYTES)
    return -ERR_NOTSUP;
  if (layout->inodes != FS_INODES || layout->root != FS_ROOT || layout->logstart != FS_LOG_START ||
      layout->logblocks != FS_LOG_BLOCKS || layout->inodemap != FS_INODE_MAP ||
      layout->blockmap != FS_BLOCK_MAP || layout->inodestart != FS_INODE_START ||
      layout->datastart != FS_DATA_START)
    return -ERR_STATE;
  return fsgeometry(device, layout->blocks, write);
}
