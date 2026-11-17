#include <pgt.h>
const Vmformat hal_vm_format = {1, 15, 0x003ffffffffffc00UL, 2, 0};
u64
hal_vm_leaf(u64 pa, u64 prot, u32 attr)
{
  return (pa >> 2) | ((prot & VM_COW) ? 512 : 0) | 1 | 64 | 128 | ((prot & VM_READ) ? 2 : 0) |
         ((prot & VM_WRITE) ? 4 : 0) | ((prot & VM_EXEC) ? 8 : 0) | (attr == VM_DEVICE ? 256 : 0) |
         ((prot & VM_USER) ? 16 : 0);
}
void
hal_vm_decode(u64 entry, Hpte *out)
{
  out->present = (entry & 1) && (entry & 14);
  out->pa = (entry & hal_vm_format.address) << 2;
  out->prot = ((entry & 2) ? VM_READ : 0) | ((entry & 4) ? VM_WRITE : 0) |
              ((entry & 8) ? VM_EXEC : 0) | ((entry & 16) ? VM_USER : 0) |
              ((entry & 512) ? VM_COW : 0);
  /* Baseline RV64 uses device PMAs; RSW records the checked mapping kind. */
  out->attr = (entry & 256) ? VM_DEVICE : VM_NORMAL;
}
