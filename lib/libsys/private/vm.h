#ifndef _LIBSYS_VM_H
#define _LIBSYS_VM_H
#include <types.h>
#define SYSVM_CHUNK   (16 * PAGE_SIZE)
#define SYSVM_REGIONS 128
#define SYSVM_PROBES  64
/* Private single-threaded address manager; not async-signal-safe. Bounds are
   supplied by the runtime, not a kernel-reserved or universally free arena. */
int   sysvmready(void);
isize sysvminit(u64 low, u64 high);
/* bytes is page-aligned. DATA pages are zeroed, RW/NX, charged to SELF_ALLOC.
   Every successful Mem management reference is dropped after map. */
isize sysvmget(u64 bytes, void **out);
/* Like sysvmget, with one reserved, unmapped page before the returned base. */
isize sysvmgetguarded(u64 bytes, void **out);
/* Exact returned base only. Failed cleanup stays reserved for later retry;
   it never makes partially mapped memory available to another allocation. */
isize sysvmput(void *address);
#endif
