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
