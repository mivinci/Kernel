#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
#include <spinlock.h>

static Proc     ptable[NPROC];
static Cpu      cpu[NCPU];
static SpinLock ptable_lock;

void proc_init(void) {
  spin_init(&ptable_lock);
  memset(ptable, 0, sizeof(ptable));
  memset(cpu, 0, sizeof(cpu));

  for (int i = 0; i < NPROC; i++) {
    ptable[i].state  = UNUSED;
    ptable[i].parent = -1;
  }

  printk("[proc] %d slots, kstack=%d KB, %d hart(s)\n", NPROC, KSTACK / 1024, NCPU);
}

int proc_create(void (*fn)(void), const char *name, void *upage) {
  Proc *p = NULL;
  int   hid = csr_read(mhartid);

  spin_lock(&ptable_lock);
  for (int i = 0; i < NPROC; i++) {
    if (ptable[i].state == UNUSED) { p = &ptable[i]; break; }
  }
  if (!p) { spin_unlock(&ptable_lock); printk("[proc] no slots\n"); return -1; }
  p->state = RUNNABLE;

  p->kstack = kalloc();
  if (!p->kstack) {
    p->state = UNUSED; spin_unlock(&ptable_lock);
    printk("[proc] kalloc failed\n"); return -1;
  }

  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (unsigned long)fn;
  p->context.sp = (unsigned long)p->kstack + KSTACK;
  p->pid        = (int)(p - ptable);
  p->upage      = upage;
  p->parent     = -1;
  p->nchild     = 0;
  p->exitcode   = 0;
  p->mscratch   = (unsigned long)p->kstack + KSTACK;

  Proc *cur = (hid < NCPU) ? cpu[hid].proc : NULL;
  if (cur && cur->nchild < 8) {
    p->parent = cur->pid;
    cur->child[cur->nchild++] = p->pid;
  }
  memcpy(p->name, name, strlen(name) + 1);
  spin_unlock(&ptable_lock);

  printk("[proc] create pid=%d kstack=%p upage=%p\n", p->pid, p->kstack, upage);
  return p->pid;
}

void scheduler(int hartid) {
  printk("[sched] hart %d starting\n", hartid);

  for (;;) {
    /* Collect unreaped zombies */
    spin_lock(&ptable_lock);
    for (int i = 0; i < NPROC; i++) {
      Proc *p = &ptable[i];
      if (p->state != ZOMBIE) continue;
      int prt = p->parent;
      if (prt < 0 || prt >= NPROC || ptable[prt].state == UNUSED) {
        kfree(p->kstack); p->kstack = NULL; p->state = UNUSED;
      }
    }
    spin_unlock(&ptable_lock);

    /* Pick next RUNNABLE process (atomic check-and-transition) */
    Proc *p = NULL;
    spin_lock(&ptable_lock);
    for (int i = 0; i < NPROC; i++) {
      if (ptable[i].state == RUNNABLE) {
        ptable[i].state = RUNNING;
        p = &ptable[i];
        break;
      }
    }
    spin_unlock(&ptable_lock);

    if (p) {
      p->mscratch = (unsigned long)p->kstack + KSTACK;
      csr_write(mscratch, p->mscratch);
      cpu[hartid].proc = p;
      swtch(&cpu[hartid].context, &p->context);
      p->mscratch = csr_read(mscratch);
      cpu[hartid].proc = NULL;
    } else {
      csr_set(mstatus, MSTATUS_MIE);
      __asm__ __volatile__("wfi");
      csr_clear(mstatus, MSTATUS_MIE);
    }
  }
}

void yield(void) {
  int   hid = csr_read(mhartid);
  Proc *p   = cpu[hid].proc;
  if (!p || p->state == ZOMBIE) return;
  spin_lock(&ptable_lock);
  p->state = RUNNABLE;
  spin_unlock(&ptable_lock);
  swtch(&p->context, &cpu[hid].context);
}

void sched_tick(void) {
  int   hid = csr_read(mhartid);
  Proc *p   = cpu[hid].proc;
  if (p && p->state == RUNNING) {
    spin_lock(&ptable_lock);
    p->state = RUNNABLE;
    spin_unlock(&ptable_lock);
    swtch(&p->context, &cpu[hid].context);
  }
}

Proc *cpu_proc(void) {
  int hid = csr_read(mhartid);
  return (hid < NCPU) ? cpu[hid].proc : NULL;
}

Proc *get_proc(int pid) {
  if (pid < 0 || pid >= NPROC) return NULL;
  return &ptable[pid];
}

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

void proc_exit(int code) {
  int   hid = csr_read(mhartid);
  Proc *p   = cpu[hid].proc;
  if (!p) return;

  p->exitcode = code;
  spin_lock(&ptable_lock);
  p->state = ZOMBIE;

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

  if (p->upage) { kfree(p->upage); p->upage = NULL; }
  if (p->satp)  { vmm_free_user_pgdir(p->satp); p->satp = 0; }

  printk("[proc] exit pid=%d code=%d\n", p->pid, code);
  swtch(&p->context, &cpu[hid].context);
  for (;;) ;
}

int proc_wait(int pid) {
  int   hid = csr_read(mhartid);
  Proc *p   = cpu[hid].proc;

  for (;;) {
    int found = 0, dead = 1;
    spin_lock(&ptable_lock);
    for (int i = 0; i < p->nchild; i++) {
      int   cid = p->child[i];
      Proc *cp  = &ptable[cid];
      if (cp->state == UNUSED) { p->child[i] = p->child[--p->nchild]; i--; continue; }
      found = 1;
      if (pid >= 0 && cid != pid) continue;
      if (cp->state == ZOMBIE) {
        printk("[proc] wait pid=%d collected child %d (exit=%d)\n",
               p->pid, cid, cp->exitcode);
        kfree(cp->kstack); cp->kstack = NULL; cp->state = UNUSED;
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
