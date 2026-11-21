#include <hal/vm.h>
#include <hal/dma.h>
#include <abi/base.h>
void
hal_code_sync(u64 pa, u64 bytes)
{
  UNUSED(pa);
  UNUSED(bytes);
  __asm__ __volatile__("fence.i" ::: "memory");
}

u64
hal_dma_granule(void)
{
  return 0;
}

int
hal_dma_sync(void *address, u64 bytes, u32 phase, u32 direction, u32 coherent)
{
  if (!address || !bytes || bytes > UINT64_MAX - (u64) address || phase > HDMA_AFTER ||
      !direction || direction > (HDMA_DEVICE_READ | HDMA_DEVICE_WRITE) || coherent > 1)
    return -ERR_INVALID;
  if (!coherent)
    return -ERR_NOTSUP;
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
  return 0;
}
