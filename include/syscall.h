#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <arch/riscv/trap.h>

/* System call numbers */
#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_YIELD  3
#define SYS_GETPID 4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_READ   7

/*
 * Invoke a system call via ecall.
 * Convention: a7 = nr, a0 = arg0, a1 = arg1, a2 = arg2.
 * Return value in a0.
 */
static inline __attribute__((always_inline)) unsigned long
syscall(unsigned long nr, unsigned long a0, unsigned long a1, unsigned long a2) {
  register unsigned long r0 asm("a0") = a0;
  register unsigned long r1 asm("a1") = a1;
  register unsigned long r2 asm("a2") = a2;
  register unsigned long r7 asm("a7") = nr;

  __asm__ __volatile__("ecall" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
  return r0;
}

void syscall_handler(TrapFrame *tf);

#endif /* _SYSCALL_H */
