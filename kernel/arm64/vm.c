#include <pgt.h>
const Vmformat hal_vm_format = {3, 3, 0x0000fffffffff000UL, 0, 0};
u64
hal_vm_leaf(u64 pa, u64 prot, u32 attr)
{
  return pa | ((prot & VM_COW) ? (1UL << 55) : 0) | 3 | (1UL << 10) |
         ((prot & VM_USER) ? (1UL << 6) | (1UL << 53) : (1UL << 54)) |
         ((prot & VM_WRITE) ? 0 : 1UL << 7) |
         ((prot & VM_EXEC) ? 0 : ((prot & VM_USER) ? (1UL << 54) : (1UL << 53))) |
         (attr == VM_NORMAL ? (3UL << 8) | (1UL << 2) : 0);
}
void
hal_vm_decode(u64 entry, Hpte *out)
{
  out->present = (entry & 3) == 3;
  out->pa = entry & hal_vm_format.address;
  out->prot = VM_READ | ((entry & (1UL << 7)) ? 0 : VM_WRITE) |
              ((entry & ((entry & (1UL << 6)) ? (1UL << 54) : (1UL << 53))) ? 0 : VM_EXEC) |
              ((entry & (1UL << 6)) ? VM_USER : 0) | ((entry & (1UL << 55)) ? VM_COW : 0);
  out->attr = (entry & (7UL << 2)) ? VM_NORMAL : VM_DEVICE;
}
