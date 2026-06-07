#include <arch/riscv/csr.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>

static Proc ptable[NPROC];
static Cpu  cpu;

void proc_init(void) {
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

  /* Find an unused slot */
  for (int i = 0; i < NPROC; i++) {
    if (ptable[i].state == UNUSED) {
      p = &ptable[i];
      break;
    }
  }
  if (!p) {
    printk("[proc] create: no free slots\n");
    return -1;
  }

  /* Allocate kernel stack (single page for now) */
  p->kstack = kalloc();
  if (!p->kstack) {
    printk("[proc] create: kalloc failed\n");
    return -1;
  }

  /* Set up initial context: sp points to top of kstack. */
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (unsigned long)fn;
  p->context.sp = (unsigned long)p->kstack + PAGE_SIZE;
  p->state      = RUNNABLE;
  p->pid        = (int)(p - ptable);
  p->upage      = upage;
  p->parent     = -1;
  p->nchild     = 0;
  p->exitcode   = 0;
  p->mscratch   = (unsigned long)p->kstack + PAGE_SIZE;

  /* Track as child of current process */
  Proc *cur = cpu_proc();
  if (cur && cur->nchild < 8) {
    p->parent = cur->pid;
    cur->child[cur->nchild++] = p->pid;
  }

  memcpy(p->name, name, strlen(name) + 1);

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
    for (int i = 0; i < NPROC; i++) {
      Proc *p = &ptable[i];
      if (p->state == RUNNABLE) {
        /* Restore mscratch: holds old_sp during trap, kstack_top otherwise */
        csr_write(mscratch, p->mscratch);

        p->state = RUNNING;
        cpu.proc = p;
        swtch(&cpu.context, &p->context);
        /* swtch() returns here when p yields or timer preempts */

        /* Save mscratch for next time */
        p->mscratch = csr_read(mscratch);
        cpu.proc = NULL;
      }
    }
  }
}

/*
 * Voluntary yield: give up the CPU and return to the scheduler.
 * Called by a running process (e.g., in a loop body).
 */
void yield(void) {
  Proc *p  = cpu.proc;
  if (!p || p->state == ZOMBIE) return;
  p->state = RUNNABLE;
  swtch(&p->context, &cpu.context);
}

/*
 * Timer-tick driven scheduling. Called from the timer interrupt handler.
 * If the current process has run long enough, preempt it.
 */
void sched_tick(void) {
  Proc *p = cpu.proc;
  if (p && p->state == RUNNING) {
    p->state = RUNNABLE;
    swtch(&p->context, &cpu.context);
  }
}

Proc *cpu_proc(void) {
  return cpu.proc;
}

/*
 * Exit current process with status code.
 * Marks process as ZOMBIE, wakes up parent waiting via proc_wait.
 * Frees user page but keeps kernel stack until parent collects.
 */
void proc_exit(int code) {
  Proc *p = cpu.proc;
  if (!p) return;

  p->exitcode = code;
  p->state    = ZOMBIE;

  /* Free user page */
  if (p->upage) {
    kfree(p->upage);
    p->upage = NULL;
  }

  printk("[proc] exit pid=%d code=%d\n", p->pid, code);

  /* Switch directly to scheduler. yield() would reset state to RUNNABLE. */
  swtch(&p->context, &cpu.context);
  /* paranoid: should never reach here since state=ZOMBIE */
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
    int         found = 0;
    int         dead  = 1;

    for (int i = 0; i < p->nchild; i++) {
      int   cid = p->child[i];
      Proc *cp  = &ptable[cid];

      if (cp->state == UNUSED) {
        /* Already collected, remove from list */
        p->child[i] = p->child[--p->nchild];
        i--;
        continue;
      }

      found = 1;

      if (pid >= 0 && cid != pid) continue;

      if (cp->state == ZOMBIE) {
        /* Collect child: free its kernel stack, mark UNUSED */
        printk("[proc] wait pid=%d collected child %d (exit=%d)\n",
               p->pid, cid, cp->exitcode);
        kfree(cp->kstack);
        cp->kstack = NULL;
        cp->state  = UNUSED;

        /* Remove from child list */
        p->child[i] = p->child[--p->nchild];
        return cid;
      }

      dead = 0; /* child alive, but not zombie */
    }

    if (!found || (pid >= 0 && dead)) return -1;

    /* No zombie children yet — yield and wait */
    yield();
  }
}
