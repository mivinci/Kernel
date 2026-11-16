#ifndef _VM_H
#define _VM_H
#include <dtb.h>
#include <hal/vm.h>
#define KBASE     0xffffffc000000000UL
#define KIRQ_DIST (KIO + PAGE_SIZE)
#define KIRQ_CPU  (KIO + 2 * PAGE_SIZE)
#define KIO       0xffffffc100000000UL
void         *kaddr(u64 pa);
u64           kphysical(const void *va);
NORETURN void vmstart(const Dtinfo *info, u32 firmware, void (*next)(Hvs *));
#endif
