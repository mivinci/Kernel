#include <arch/riscv/csr.h>
#include <fs.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <proc.h>
#include <syscall.h>
#include <uart.h>

/*
 * System call: write to a file descriptor.
 *   a0 = fd
 *   a1 = buf pointer
 *   a2 = length
 */
static unsigned long sys_write(TrapFrame *tf) {
  int           fd  = tf->a0;
  unsigned long len = tf->a2;

  if (fd == 1) { /* stdout: write to UART */
    for (unsigned long i = 0; i < len; i++)
      putc(((char *)tf->a1)[i]);
    return len;
  }

  File *f = fdget(fd);
  if (!f || f->writable == 0) return -1;

  Inode *ip = f->ip;
  if (!ip) return -1;

  igrow(ip, f->off + len);
  memcpy(ip->data + f->off, (void *)tf->a1, len);
  f->off += len;
  if (f->off > ip->size) ip->size = f->off;
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
    if (p->kstack) kfree(p->kstack);
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

/*
 * System call: open a file.
 *   a0 = filename (char *)
 *   a1 = flags (0=RDONLY, 1=WRONLY, 2=RDWR)
 * Returns fd on success, -1 on error.
 */
static unsigned long sys_open(TrapFrame *tf) {
  const char *name  = (const char *)tf->a0;
  int         flags = tf->a1;

  printk("[sys_open] name=%p flags=%d\n", name, flags);

  Inode *ip = ialloc(name);
  if (!ip) return -1;

  File *f = (File *)kalloc();
  if (!f) {
    ifree(ip);
    return -1;
  }

  memset(f, 0, sizeof(File));
  f->type     = FD_INODE;
  f->ip       = ip;
  f->ref      = 0;
  f->off      = 0;
  f->readable = 1;
  f->writable = (flags != 0); /* non-zero flags = writable */

  int fd = fdalloc(f);
  if (fd < 0) {
    kfree(f);
    ifree(ip);
  }
  return fd;
}

/*
 * System call: close a file descriptor.
 *   a0 = fd
 */
static unsigned long sys_close(TrapFrame *tf) {
  fdclose(tf->a0);
  return 0;
}

/*
 * System call: read from a file descriptor.
 *   a0 = fd
 *   a1 = buf pointer
 *   a2 = length
 * Returns bytes read, -1 on error.
 */
static unsigned long sys_read(TrapFrame *tf) {
  int           fd  = tf->a0;
  unsigned long len = tf->a2;

  if (fd == 0) { /* stdin: not supported yet */
    return 0;
  }

  File *f = fdget(fd);
  if (!f || f->readable == 0) return -1;

  Inode *ip = f->ip;
  if (!ip) return -1;

  if (f->off >= ip->size) return 0; /* EOF */

  if (f->off + len > (unsigned long)ip->size) len = ip->size - f->off;

  memcpy((void *)tf->a1, ip->data + f->off, len);
  f->off += len;
  return len;
}

typedef unsigned long (*SysFn)(TrapFrame *);

static SysFn syscall_table[] = {
  [SYS_WRITE] = sys_write,   [SYS_EXIT] = (SysFn)sys_exit, [SYS_YIELD] = (SysFn)sys_yield,
  [SYS_GETPID] = sys_getpid, [SYS_OPEN] = sys_open,        [SYS_CLOSE] = sys_close,
  [SYS_READ] = sys_read,
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
