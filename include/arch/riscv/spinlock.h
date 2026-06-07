#ifndef _ARCH_RISCV_SPINLOCK_H
#define _ARCH_RISCV_SPINLOCK_H

#include <libc.h>

XDEF_STRUCT(SpinLock) {
  int locked;
};

/*
 * Acquire a spinlock. Spins until the lock is available.
 * Uses amoswap.w for atomic test-and-set.
 */
static inline void spin_lock(SpinLock *lk) {
  int old;
  __asm__ __volatile__("1:"
                       "  amoswap.w %0, %1, (%2)\n"
                       "  bnez     %0, 1b"
                       : "=&r"(old)
                       : "r"((int)1), "r"(&lk->locked)
                       : "memory");
}

/*
 * Release a spinlock.
 */
static inline void spin_unlock(SpinLock *lk) {
  __asm__ __volatile__("amoswap.w x0, x0, (%0)" : : "r"(&lk->locked) : "memory");
}

/*
 * Initialize a spinlock to the unlocked state.
 */
static inline void spin_init(SpinLock *lk) {
  lk->locked = 0;
}

#endif /* _ARCH_RISCV_SPINLOCK_H */
