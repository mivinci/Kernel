/* Trusted construction/dispatch slice, not native create/start authorization. */
#ifndef _PROC_H
#define _PROC_H
#include <life.h>
#include <local.h>
#include <kstack.h>
#include <abi/proc.h>
struct Hctx;
#define PROC_BUILDING  5
#define PROC_CONSTRUCT 1
#define PROC_READY     2
#define PROC_RUNNING   3
#define PROC_EXITING   4

typedef struct Procinfo Procinfo;
struct Procinfo
{
  Handle   pid;
  Lifeid   life;
  Acctid   acct;
  Spaceid  space;
  Kstackid stack;
  u64      image;
  u32      state, configured, phase, resources;
};
/* Parent/set/policy supplied by trusted boot/test code. No raw PID authority is
   exported to userspace. Rq/image/preparation admission is not implemented yet. */
int     procnew(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, Handle *out);
int     procprepare(Rtable *source, Handle target, u64 rights, Handle *out);
int     procwork(u32 scans);
int     procinfo(Handle pid, Procinfo *out);
Rtable *proctable(Handle pid);
int     procconfigure(Handle pid, const Config *config, u32 reserve);
int     procpublish(Handle pid);
int     procdestroy(Handle pid);
/* A single cooperative ready continuation. No timer/preemptive scheduler yet. */
int     procrunone(void);
Rtable *proccurrent(struct Hctx *frame);
int     procidentity(Handle *pid, u64 *image, Handle *parent);
void    procyield(void);
void    procpausevm(void);
void    procresumevm(void);
/* Called on the running thread after complete user state has been saved. */
NORETURN void procexit(u32 reason, u32 code, const Lifefault *fault);
/* One cleanup phase or bounded Local/Mmap batch; off the target stack. */
int procclean(Handle pid);
#endif
