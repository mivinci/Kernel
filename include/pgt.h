/* Private encoding shared by the table walker and architecture backends. */
#ifndef _PGT_H
#define _PGT_H
#include <hal/vm.h>
typedef struct Vmformat Vmformat;
struct Vmformat
{
  u64 table, tablemask, address;
  u32 shift, reserved;
};
extern const Vmformat hal_vm_format;
u64                   hal_vm_leaf(u64 pa, u64 prot, u32 attr);
void                  hal_vm_decode(u64 entry, Hpte *out);
#endif
