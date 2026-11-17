/* A single deliberate fault, linked only into its explicitly selected image. */
#include <boot.h>
#include <diag.h>
#include <vm.h>
#include <page.h>
#include "ktest.h"

#if BOOT_CASE == 11
static u64
vmmaptest(Hvs *space)
{
  Pgblk         block, replacement;
  Pgstat        before, after;
  Hpte          entry;
  u64           target = KIO + (1UL << 21), tables = space->pages;
  volatile u64 *mapped = (volatile u64 *) target;
  pgstat(&before);
  if (pgalloc(0, NULL, &block))
    panic("VM test page");
  if (hal_vm_map(space, target, block.pa, VM_READ | VM_WRITE, VM_NORMAL))
    panic("VM test map");
  hal_vm_commit(space);
  *mapped = 0xabcddcba;
  if (*(volatile u64 *) kaddr(block.pa) != 0xabcddcba || !hal_vm_lookup(space, target, &entry) ||
      entry.pa != block.pa)
    panic("VM alias data");
  if (hal_vm_unmap(space, target) || space->pages != tables || hal_vm_lookup(space, target, &entry))
    panic("VM unmap/reclaim");
  if (pgfree(block) || pgalloc(0, NULL, &replacement))
    panic("VM physical reuse");
  if (replacement.pa != block.pa)
    panic("VM deterministic reuse");
  *(volatile u64 *) kaddr(replacement.pa) = 0x12344321;
  if (pgfree(replacement) || pgcheck())
    panic("VM test release");
  pgstat(&after);
  if (after.free != before.free || after.allocated != before.allocated)
    panic("VM page rollback");
  return target;
}
#endif

static volatile u32 instruction;
void
vmtest(Hvs *space)
{
  u64 target;
  UNUSED(space);
#if BOOT_CASE == 4
  target = (u64) __text_start;
#elif BOOT_CASE == 5
  target = (u64) __rodata_start;
  {
    Hpte          expected;
    volatile u64 *mapped = (volatile u64 *) target;
    u64           value;
    if (!hal_vm_lookup(space, target, &expected) || expected.prot != VM_READ ||
        hal_vm_replace(space, target, &expected, expected.pa, VM_READ | VM_WRITE))
      panic("PTE permission warmup");
    value = *mapped;
    *mapped = value; /* Prime an actual writable translation before revoking. */
    if (!hal_vm_lookup(space, target, &expected) ||
        hal_vm_replace(space, target, &expected, expected.pa, VM_READ) ||
        !hal_vm_lookup(space, target, &expected) || expected.prot != VM_READ)
      panic("PTE permission downgrade");
    putstr("PTEPERMCHECK warmed=");
    puthex(1);
    putstr(" downgraded=");
    puthex(1);
    putstr("\n");
  }
#elif BOOT_CASE == 6
  target = (u64) &instruction;
#elif BOOT_CASE == 7
  target = (u64) __stack_guard;
#elif BOOT_CASE == 8
  target = 0;
#elif BOOT_CASE == 9
  target = kphysical(__text_start);
#elif BOOT_CASE == 10
  target = KBASE & ((1UL << 39) - 1);
#else
  target = vmmaptest(space);
#endif
  UNUSED(instruction);
  putstr("VMTEST kind=");
  puthex(BOOT_CASE);
  putstr(" target=");
  puthex(target);
  putstr("\n");
  arch_vmfault(target);
  panic("permission fault returned");
}
