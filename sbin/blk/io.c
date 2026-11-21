#include "io.h"

int
blkiosubmit(Vblkreq *request, Vring *ring, const Vblkconfig *config, const Blkdma *dma, u32 type,
            u64 sector, u32 bytes, u64 cookie, const void *source, Vblksync sync, void *context)
{
  const u8 *input = source;
  u32       i;
  if (!request || !dma || !dma->header || !dma->status || bytes > dma->capacity ||
      (bytes && !dma->data) || ((type == VBLK_WRITE) != (source != NULL)))
    return -ERR_INVALID;
  if (request->held || request->active)
    return -ERR_BUSY;
  if (type == VBLK_WRITE)
    for (i = 0; i < bytes; i++)
      dma->data[i] = input[i];
  return vblksubmit(request, ring, config, type, sector, cookie, dma->headaddr,
                    bytes ? dma->dataaddr : 0, bytes, dma->statusaddr, dma->header, dma->status,
                    sync, context);
}

int
blkiofinish(Vblkreq *request, const Vdone *done, const Blkdma *dma, void *destination, u32 capacity)
{
  u8 *output = destination;
  u32 i;
  int result;
  if (!request || !done || !dma || request->status != dma->status)
    return -ERR_INVALID;
  if (output && (request->type != VBLK_READ || request->buffers[1].address != dma->dataaddr ||
                 !dma->data || request->bytes > dma->capacity || request->bytes > capacity))
    return -ERR_INVALID;
  if (!output && capacity)
    return -ERR_INVALID;
  result = vblkfinish(request, done);
  if (result == VBLK_OK && output)
    for (i = 0; i < request->bytes; i++)
      output[i] = dma->data[i];
  return result;
}
