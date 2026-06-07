#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <proc.h>

static Proc ptable[NPROC];
static Cpu  cpu;

void proc_init(void) {
  memset(ptable, 0, sizeof(ptable));
  memset(&cpu, 0, sizeof(cpu));

  for (int i = 0; i < NPROC; i++)
    ptable[i].state = UNUSED;

  printk("[proc] %d slots, kstack=%d KB\n", NPROC, KSTACK / 1024);
}

/*
 * Create a new kernel thread. Allocates kernel stack, sets up
 * initial context so that swtch() starts running fn().
 */
int proc_create(void (*fn)(void), const char *name) {
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
  p->pid        = p - ptable;
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
        p->state = RUNNING;
        cpu.proc = p;
        swtch(&cpu.context, &p->context);
        /* swtch() returns here when p yields or timer preempts */
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
  p->state = RUNNABLE;
  swtch(&p->context, &cpu.context);
}

/*
 * Timer-tick driven scheduling. Called from the timer interrupt handler.
 * If the current process has run long enough, preempt it.
 */
void sched_tick(void) {
  if (cpu.proc && cpu.proc->state == RUNNING) {
    cpu.proc->state = RUNNABLE;
    swtch(&cpu.proc->context, &cpu.context);
  }
}

Proc *cpu_proc(void) {
  return cpu.proc;
}
