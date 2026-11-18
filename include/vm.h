#ifndef _VM_H
#define _VM_H
#include <dtb.h>
#include <hal/vm.h>
#include <layout.inc>
#define KIRQ_DIST   (KIO + PAGE_SIZE)
#define KIRQ_CPU    (KIO + 2 * PAGE_SIZE)
#define KIRQ_ENABLE (KIO + 3 * PAGE_SIZE)
STATIC_ASSERT(KMAP_BYTES <= KIO - KBASE, direct_map_before_devices);
void         *kaddr(u64 pa);
u64           kphysical(const void *va);
NORETURN void vmstart(const Dtinfo *info, u32 firmware, u64 cpu, void (*next)(Hvs *));
#endif
