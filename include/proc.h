#ifndef _PROC_H
#define _PROC_H

#include <libc.h>

#define NPROC   16
#define KSTACK  (4 * 4096) /* 16 KB kernel stack per process */

typedef enum {
  UNUSED,
  RUNNABLE,
  RUNNING,
} ProcState;

/* Callee-saved registers saved by swtch() */
XDEF_STRUCT(Context) {
  unsigned long ra;
  unsigned long sp;
  unsigned long s0;
  unsigned long s1;
  unsigned long s2;
  unsigned long s3;
  unsigned long s4;
  unsigned long s5;
  unsigned long s6;
  unsigned long s7;
  unsigned long s8;
  unsigned long s9;
  unsigned long s10;
  unsigned long s11;
};

/* Per-process state */
XDEF_STRUCT(Proc) {
  Context   context;   /* swtch() save/restore area */
  void      *kstack;   /* kernel stack bottom */
  ProcState state;     /* UNUSED / RUNNABLE / RUNNING */
  int       pid;       /* process id */
  char      name[16]; /* debug name */
};

/* Per-CPU state */
XDEF_STRUCT(Cpu) {
  Proc    *proc;    /* currently running process */
  Context  context; /* swtch() save area for scheduler */
};

void proc_init(void);
void scheduler(void) __attribute__((noreturn));
void swtch(Context *old, Context *new);
int  proc_create(void (*fn)(void), const char *name);
void yield(void);
void sched_tick(void);
Proc *cpu_proc(void);

#endif /* _PROC_H */
