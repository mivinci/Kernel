#include <arch/riscv/csr.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <proc.h>
#include <syscall.h>
#include <uart.h>

/*
 * System call: write to stdout (UART)
 *   a0 = fd (ignored, always UART for now)
 *   a1 = buf pointer
 *   a2 = length
 * Returns number of bytes written.
 */
static unsigned long sys_write(TrapFrame *tf) {
  unsigned long len = tf->a2;
  for (unsigned long i = 0; i < len; i++) {
    putc(((char *)tf->a1)[i]);
  }
  return len;
}

/*
 * System call: exit the current process.
 *   a0 = exit code (unused for now)
 */
static void sys_exit(TrapFrame *tf) {
  (void)tf; /* exit code in a0, unused */
  Proc *p = cpu_proc();
  if (p) {
    p->state = UNUSED;
    if (p->kstack)
      kfree(p->kstack);
    p->kstack = NULL;
  }
  yield();
  /* yield() should not return here — the scheduler will never
   * switch back to this process since it's UNUSED. */
  for (;;)
    ;
}

/*
 * System call: voluntarily yield the CPU.
 */
static void sys_yield(TrapFrame *tf) {
  (void)tf;
  yield();
}

/*
 * System call: get current process id.
 * Returns the pid.
 */
static unsigned long sys_getpid(TrapFrame *tf) {
  (void)tf;
  Proc *p = cpu_proc();
  return p ? p->pid : -1;
}

typedef unsigned long (*SysFn)(TrapFrame *);

static SysFn syscall_table[] = {
    [SYS_WRITE] = sys_write,
    [SYS_EXIT] = (SysFn)sys_exit,
    [SYS_YIELD] = (SysFn)sys_yield,
    [SYS_GETPID] = sys_getpid,
};

#define NSYSCALLS (sizeof(syscall_table) / sizeof(syscall_table[0]))

/*
 * Handle an ECALL from M-mode.
 * Read syscall number from a7, arguments from a0-a5,
 * call handler, store return value in a0.
 */
void syscall_handler(TrapFrame *tf) {
  unsigned long nr = tf->a7;

  if (nr < NSYSCALLS && syscall_table[nr]) {
    tf->a0 = syscall_table[nr](tf);
  } else {
    printk("[syscall] unknown syscall %d\n", nr);
    tf->a0 = -1;
  }

  /* Advance mepc past the ecall instruction (4 bytes) */
  tf->mepc += 4;
}
