#include <hal/vm.h>
#include <hal/dma.h>
#include <abi/base.h>
#include <vm.h>
#include <boot.h>
#include <diag.h>

/* Called only from the physical MMU transition, after all table writes. */
static void
bootinvalidate(u64 base, u64 bytes, u64 line)
{
  u64 address;
  if ((base | bytes) & PAGE_MASK || !bytes || bytes > ~(u64) 0 - base)
    panic("boot cache range");
  __asm__ __volatile__("dsb sy" ::: "memory");
  for (address = base; address < base + bytes; address += line)
    __asm__ __volatile__("dc ivac, %0" ::"r"(address) : "memory");
  __asm__ __volatile__("dsb sy" ::: "memory");
}
void
hal_boot_cache_prepare(u64 root, const Dtinfo *info, u32 firmware)
{
  const u64 *top, *middle;
  u64        ctr, control, line, pa, end;
  u32        i, j, r;
  __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
  __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(control));
  line = 4UL << ((ctr >> 16) & 15);
  if ((control & 5) || line > PAGE_SIZE || !info || firmware > info->nreserved ||
      info->nreserved > DT_RESERVED || info->nram > DT_RAM || !root || root & PAGE_MASK)
    panic("boot cache geometry");
  for (i = firmware; i < info->nreserved; i++) {
    Dtrange range = info->reserved[i];
    if (range.base == (u64) __kernel_start &&
        range.size == (u64) __kernel_end - (u64) __kernel_start)
      continue; /* Assembly finishes with the kernel image and live stack. */
    if (!range.size || range.size > ~(u64) PAGE_MASK - range.base)
      panic("boot cache reservation");
    pa = ALIGN_DOWN(range.base, PAGE_SIZE);
    end = ALIGN_UP(range.base + range.size, PAGE_SIZE);
    for (r = 0; r < info->nram; r++)
      if (pa >= info->ram[r].base && pa - info->ram[r].base < info->ram[r].size &&
          end - pa <= info->ram[r].size - (pa - info->ram[r].base))
        break;
    if (r == info->nram)
      panic("boot cache outside RAM");
    bootinvalidate(pa, end - pa, line);
  }
  /* Tables are exclusive allocated RAM; only 4 KiB descriptors are supported.
     Invalidate children after construction, including the temporary low root. */
  top = (const u64 *) root;
  for (i = 0; i < 512; i++) {
    if (!top[i])
      continue;
    if ((top[i] & 3) != 3)
      panic("boot cache root");
    middle = (const u64 *) (top[i] & 0x0000fffffffff000UL);
    for (j = 0; j < 512; j++) {
      if (!middle[j])
        continue;
      if ((middle[j] & 3) != 3)
        panic("boot cache table");
      bootinvalidate(middle[j] & 0x0000fffffffff000UL, PAGE_SIZE, line);
    }
    bootinvalidate((u64) middle, PAGE_SIZE, line);
  }
  bootinvalidate(root, PAGE_SIZE, line);
}
void
hal_code_sync(u64 pa, u64 bytes)
{
  u64 ctr, line, address, end;
  __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
  line = 4UL << ((ctr >> 16) & 15);
  address = (u64) kaddr(pa);
  end = address + bytes;
  for (address = ALIGN_DOWN(address, line); address < end; address += line)
    __asm__ __volatile__("dc cvau, %0" ::"r"(address) : "memory");
  __asm__ __volatile__("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
}

static u64
cachetype(void)
{
  u64 ctr;
  __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
  return ctr;
}

static u64
dmagrain(u64 ctr)
{
  u64 line = 4UL << ((ctr >> 16) & 15), cwg = (ctr >> 24) & 15;
  u64 writeback = 4UL << cwg;
  if (!cwg || line > PAGE_SIZE || writeback > PAGE_SIZE)
    return 0;
  return MAX(line, writeback);
}

u64
hal_dma_granule(void)
{
  return dmagrain(cachetype());
}

int
hal_dma_sync(void *address, u64 bytes, u32 phase, u32 direction, u32 coherent)
{
  u64 va = (u64) address, ctr, grain, line, end;
  if (!address || !bytes || bytes > UINT64_MAX - va || phase > HDMA_AFTER || !direction ||
      direction > (HDMA_DEVICE_READ | HDMA_DEVICE_WRITE) || coherent > 1)
    return -ERR_INVALID;
  if (!coherent) {
    ctr = cachetype();
    grain = dmagrain(ctr);
    if (!grain)
      return -ERR_NOTSUP;
    if ((va | bytes) & (grain - 1))
      return -ERR_INVALID;
    line = 4UL << ((ctr >> 16) & 15);
    end = va + bytes;
    __asm__ __volatile__("dsb sy" ::: "memory");
    if (phase == HDMA_BEFORE) {
      for (; va < end; va += line)
        __asm__ __volatile__("dc cvac, %0" : : "r"(va) : "memory");
    } else if (direction & HDMA_DEVICE_WRITE) {
      for (; va < end; va += line)
        __asm__ __volatile__("dc ivac, %0" : : "r"(va) : "memory");
    }
  }
  __asm__ __volatile__("dsb sy" ::: "memory");
  return 0;
}
