#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <spinlock.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>

static Proc     ptable[NPROC];
static Cpu      cpu;
static SpinLock ptable_lock;

void proc_init(void) {
  spin_init(&ptable_lock);
  memset(ptable, 0, sizeof(ptable));
  memset(&cpu, 0, sizeof(cpu));

  for (int i = 0; i < NPROC; i++) {
    ptable[i].state  = UNUSED;
    ptable[i].parent = -1;
  }

  printk("[proc] %d slots, kstack=%d KB\n", NPROC, KSTACK / 1024);
}

/*
 * Create a new process. Allocates kernel stack, sets up
 * initial context so that swtch() starts running fn().
 * upage is the user binary page (NULL for kernel-only threads).
 */
int proc_create(void (*fn)(void), const char *name, void *upage) {
  Proc *p = NULL;

  spin_lock(&ptable_lock);

  /* Find an unused slot */
  for (int i = 0; i < NPROC; i++) {
    if (ptable[i].state == UNUSED) {
      p = &ptable[i];
      break;
    }
  }
  if (!p) {
    spin_unlock(&ptable_lock);
    printk("[proc] create: no free slots\n");
    return -1;
  }

  /* Reserve the slot early so kalloc failure returns it to pool */
  p->state = RUNNABLE;

  /* Allocate kernel stack */
  p->kstack = kalloc();
  if (!p->kstack) {
    p->state = UNUSED;
    spin_unlock(&ptable_lock);
    printk("[proc] create: kalloc failed\n");
    return -1;
  }

  /* Set up initial context: sp points to top of kstack. */
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (unsigned long)fn;
  p->context.sp = (unsigned long)p->kstack + KSTACK;
  p->pid        = (int)(p - ptable);
  p->upage      = upage;
  p->parent     = -1;
  p->nchild     = 0;
  p->exitcode   = 0;
  p->mscratch   = (unsigned long)p->kstack + KSTACK;

  printk("[proc] create pid=%d kstack=%p upage=%p\n", p->pid, p->kstack, upage);

  /* Track as child of current process */
  Proc *cur = cpu_proc();
  if (cur && cur->nchild < 8) {
    p->parent = cur->pid;
    cur->child[cur->nchild++] = p->pid;
  }

  memcpy(p->name, name, strlen(name) + 1);
  spin_unlock(&ptable_lock);

  return p->pid;
}

/*
 * Simple round-robin scheduler. Never returns.
 * Called from swtch() which saves the old context and restores
 * this scheduler's context.
 */
void scheduler(void) {
  printk("[sched] starting round-robin\n");

  for (;;) {
    int found = 0;

    /* Collect unreaped zombies with dead/no parent */
    spin_lock(&ptable_lock);
    for (int i = 0; i < NPROC; i++) {
      Proc *p = &ptable[i];
      if (p->state != ZOMBIE) continue;
      int parent = p->parent;
      if (parent < 0 || parent >= NPROC || ptable[parent].state == UNUSED) {
        kfree(p->kstack);
        p->kstack = NULL;
        p->state  = UNUSED;
      }
    }
    spin_unlock(&ptable_lock);

    for (int i = 0; i < NPROC; i++) {
      Proc *p = &ptable[i];
      if (p->state == RUNNABLE) {
        found = 1;

        csr_write(mscratch, p->mscratch);
        p->state = RUNNING;
        cpu.proc = p;
        swtch(&cpu.context, &p->context);

        p->mscratch = csr_read(mscratch);
        cpu.proc = NULL;
      }
    }

    /* No runnable process — wait for interrupt */
    if (!found) {
      csr_set(mstatus, MSTATUS_MIE);
      __asm__ __volatile__("wfi");
      csr_clear(mstatus, MSTATUS_MIE);
    }
  }
}

/*
 * Voluntary yield: give up the CPU and return to the scheduler.
 * Called by a running process (e.g., in a loop body).
 */
void yield(void) {
  Proc *p = cpu.proc;
  if (!p || p->state == ZOMBIE) return;
  spin_lock(&ptable_lock);
  p->state = RUNNABLE;
  spin_unlock(&ptable_lock);
  swtch(&p->context, &cpu.context);
}

void sched_tick(void) {
  Proc *p = cpu.proc;
  if (p && p->state == RUNNING) {
    spin_lock(&ptable_lock);
    p->state = RUNNABLE;
    spin_unlock(&ptable_lock);
    swtch(&p->context, &cpu.context);
  }
}

Proc *cpu_proc(void) {
  return cpu.proc;
}

Proc *get_proc(int pid) {
  if (pid < 0 || pid >= NPROC) return NULL;
  return &ptable[pid];
}

/*
 * Wake the first IOWAIT process (block device interrupt handler).
 */
void proc_iowait_wake(void) {
  spin_lock(&ptable_lock);
  for (int i = 0; i < NPROC; i++) {
    if (ptable[i].state == IOWAIT) {
      ptable[i].state = RUNNABLE;
      break;
    }
  }
  spin_unlock(&ptable_lock);
}

/*
 * Exit current process with status code.
 * Marks process as ZOMBIE.  Frees user page here, kstack is
 * freed by proc_wait or the scheduler (for unreaped zombies).
 */
void proc_exit(int code) {
  Proc *p = cpu.proc;
  if (!p) return;

  p->exitcode = code;

  spin_lock(&ptable_lock);
  p->state = ZOMBIE;

  /* Orphan children to init (pid 0) */
  Proc *init = &ptable[0];
  for (int i = 0; i < p->nchild; i++) {
    int   cid = p->child[i];
    Proc *cp  = &ptable[cid];
    if (cp->state == UNUSED) continue;
    cp->parent = 0;
    if (init->state != UNUSED && init->nchild < 8)
      init->child[init->nchild++] = cid;
  }
  p->nchild = 0;
  spin_unlock(&ptable_lock);

  /* Free user page — kstack stays until parent collects */
  if (p->upage) {
    kfree(p->upage);
    p->upage = NULL;
  }

  /* Free user page table */
  if (p->satp) {
    vmm_free_user_pgdir(p->satp);
    p->satp = 0;
  }

  printk("[proc] exit pid=%d code=%d\n", p->pid, code);

  /* Switch out — state=ZOMBIE, scheduler won't pick us again. */
  swtch(&p->context, &cpu.context);
  /* paranoid */
  for (;;)
    ;
}

/*
 * Wait for a child process to exit.
 * pid = -1: wait for any child.
 * Returns the child pid, or -1 if no children to wait for.
 */
int proc_wait(int pid) {
  Proc *p = cpu.proc;

  for (;;) {
    int found = 0;
    int dead  = 1;

    spin_lock(&ptable_lock);
    for (int i = 0; i < p->nchild; i++) {
      int   cid = p->child[i];
      Proc *cp  = &ptable[cid];

      if (cp->state == UNUSED) {
        p->child[i] = p->child[--p->nchild];
        i--;
        continue;
      }

      found = 1;
      if (pid >= 0 && cid != pid) continue;

      if (cp->state == ZOMBIE) {
        printk("[proc] wait pid=%d collected child %d (exit=%d)\n",
               p->pid, cid, cp->exitcode);
        kfree(cp->kstack);
        cp->kstack = NULL;
        cp->state  = UNUSED;
        p->child[i] = p->child[--p->nchild];
        spin_unlock(&ptable_lock);
        return cid;
      }
      dead = 0;
    }
    spin_unlock(&ptable_lock);

    if (!found || (pid >= 0 && dead)) return -1;
    yield();
  }
}
