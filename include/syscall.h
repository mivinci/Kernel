#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <arch/riscv/trap.h>

/* System call numbers */
#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_YIELD  3
#define SYS_GETPID 4

/*
 * Invoke a system call:
 *   nr   = syscall number (SYS_*)
 *   args = up to 3 register arguments
 * Returns the value in a0 after ecall.
 */
static inline unsigned long syscall(unsigned long nr, unsigned long a0,
                                    unsigned long a1, unsigned long a2) {
  register unsigned long _a0 asm("a0") = a0;
  register unsigned long _a1 asm("a1") = a1;
  register unsigned long _a2 asm("a2") = a2;
  register unsigned long _nr asm("a7") = nr;

  __asm__ __volatile__("ecall"
                       : "+r"(_a0)
                       : "r"(_a1), "r"(_a2), "r"(_nr)
                       : "memory");
  return _a0;
}

void syscall_handler(TrapFrame *tf);

#endif /* _SYSCALL_H */
