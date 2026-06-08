#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <chr.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
#include <syscall.h>
#include <tty.h>
#include <usr.h>

/*
 * Translate a user virtual address to physical, for kernel access.
 * Returns 0 if unmapped.
 */
static inline char *user_ptr(unsigned long va) {
  unsigned long pa = user_va2pa(va);
  return pa ? (char *)pa : NULL;
}

/*
 * System call: write to a file descriptor.
 *   a0 = fd
 *   a1 = buf pointer
 *   a2 = length
 */
static unsigned long sys_write(TrapFrame *tf) {
  int           fd  = tf->a0;
  unsigned long len = tf->a2;

  if (fd == 1) { /* stdout: write to console */
    char *buf = user_ptr(tf->a1);
    if (!buf) return -1;
    for (unsigned long i = 0; i < len; i++)
      chr_write(buf[i]);
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
 *   a0 = exit code
 */
static void sys_exit(TrapFrame *tf) {
  proc_exit((int)tf->a0);
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
 * System call: spawn a new user process from ramfs.
 *   a0 = path (char *)
 * Returns child pid on success, -1 on error.
 */
static unsigned long sys_spawn(TrapFrame *tf) {
  char *path = user_ptr(tf->a0);
  if (!path) return -1;
  return usr_spawn(path);
}

/*
 * System call: wait for child to exit.
 *   a0 = pid (-1 for any child)
 * Returns child pid, or -1 if no children.
 */
static unsigned long sys_wait(TrapFrame *tf) {
  return proc_wait((int)tf->a0);
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

  if (fd == 0) { /* stdin: read from TTY (blocking, with poll fallback) */
    char *buf = user_ptr(tf->a1);
    if (!buf) return -1;
    /* Poll fallback: drain any chars that missed the interrupt */
    while (chr_has_data())
      tty_input(&console_tty, (char)chr_read());
    return tty_read(&console_tty, buf, (int)len);
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
  [SYS_READ] = sys_read,     [SYS_SPAWN] = sys_spawn,      [SYS_WAIT] = sys_wait,
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
