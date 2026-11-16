/* Trusted construction/dispatch slice, not native create/start authorization. */
#ifndef _PROC_H
#define _PROC_H
#include <life.h>
#include <image.h>
#include <local.h>
#include <kstack.h>
#include <abi/proc.h>
struct Hctx;
#define PROC_BLOCKED   6
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
  Imageid  identity;
  Acctid   acct;
  Spaceid  space;
  Kstackid stack;
  u64      image;
  u32      state, configured, phase, resources;
};
/* Parent/set/policy supplied by trusted boot/test code. No raw PID authority is
   exported to userspace. Full native CREATE policy/admission remains pending. */
int procnew(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, Handle *out);
/* Native fixed-Life CONTROL operations; construction admission stays gated. */
int procgrant(Rtable *source, Handle target, Handle resource, u64 rights, u64 offset, u64 length,
              u64 inherit, Handle *out);
int procrevoke(Rtable *source, Handle target, Handle resource);
int procbudget(Rtable *source, Handle target, const Budgetarg *args);
int procterminate(Rtable *source, Handle target);
int proccred(Rtable *source, Handle subject, Handle pid, u64 image, Cred *out);
int procgetpg(Handle pid, Handle *pgid, Handle *sid);
int procsetpg(Handle pid, Handle pgid);
int procsetsid(Handle *sid);
int procquery(Rtable *source, Handle authority, u64 kind, u64 cursor, u64 address, u64 count,
              u64 *written, u64 *next);
int procprepare(Rtable *source, Handle target, u64 rights, Handle *out);
int procwork(u32 scans);
int procinfo(Handle pid, Procinfo *out);
Rtable *proctable(Handle pid);
int     procconfigure(Handle pid, const Config *config, u32 reserve);
int     procpublish(Handle pid);
int     procdestroy(Handle pid);
/* CPU dispatcher selects one runnable continuation with IRQs masked. */
int     procrunone(void);
Rtable *proccurrent(struct Hctx *frame);
isize   procipc(u64 number, const u64 args[7], u64 outputs[8]);
int     procidentity(Handle *pid, u64 *image, Handle *parent);
void    procyield(void);
void    procpausevm(void);
void    procresumevm(void);
/* Called on the running thread after complete user state has been saved. */
NORETURN void procexit(u32 reason, u32 code, const Lifefault *fault);
/* One cleanup phase or bounded Local/Mmap batch; off the target stack. */
int procclean(Handle pid);
/* CPU dispatcher enables time slicing once, before selecting user processes. */
int           proctimerstart(void);
u64           procticks(void);
u64           prockernelticks(void);
void          prockernelirq(void);
void          proccheckpoint(void);
int           procidle(void);
NORETURN void proctimertrap(struct Hctx *context);
#endif
