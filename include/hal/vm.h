#ifndef _HAL_VM_H
#define _HAL_VM_H
#define HVS_ROOT 0
#define HVS_USER 48
#ifndef __ASSEMBLER__
#include <types.h>
#define VM_READ   1
#define VM_WRITE  2
#define VM_EXEC   4
#define VM_USER   8 /* lookup-only: map derives user access from the root */
#define VM_NORMAL 0
#define VM_DEVICE 1
#define VM_BAD    -1
#define VM_NOMEM  -2
#define VM_EXISTS -3

typedef struct Pgsource Pgsource;
typedef struct Hvs      Hvs;
typedef struct Hpte     Hpte;
struct Pgsource
{
  int (*alloc)(void *context, u64 *pa);
  void (*free)(void *context, u64 pa);
  void *(*access)(void *context, u64 pa);
  void *context;
};
struct Hvs
{
  u64      root;
  u64      pages;
  Pgsource source;
  u64      user;
};
struct Hpte
{
  u64 pa, prot, attr, present;
};
STATIC_ASSERT(offsetof(Hvs, root) == HVS_ROOT, hvs_root);
STATIC_ASSERT(offsetof(Hvs, user) == HVS_USER, hvs_user);
STATIC_ASSERT(sizeof(Hpte) == 32, hpte_size);
/* Mapping is an internal mechanism; the caller enforces authorization and policy.
   Commit after successful additions; unmap invalidates before reclaiming tables.
   Pgsource owns aligned, ordinary RAM frames and supplies access in the current VA. */
isize hal_vm_create(Hvs *vs, Pgsource *source);
/* Fresh inactive root borrows the permanent kernel high half. */
isize hal_vm_user(Hvs *vs, const Hvs *kernel);
isize hal_vm_map(Hvs *vs, u64 va, u64 pa, u64 prot, u32 attr);
isize hal_vm_unmap(Hvs *vs, u64 va);
u32   hal_vm_lookup(const Hvs *vs, u64 va, Hpte *out);
void  hal_vm_switch(const Hvs *vs);
void  hal_vm_commit(Hvs *vs);
void  hal_code_sync(u64 pa, u64 bytes);
/* Caller must have stopped using this root on the CPU before destroying it. */
void hal_vm_destroy(Hvs *vs);
/* Bootstrap transition only; no user context switch contract yet. */
NORETURN void hal_vm_enter(u64 root, u64 delta, void (*next)(void));
void          hal_vm_drop_low(Hvs *vs);
#endif
#endif
