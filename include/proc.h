/* Process construction, native authority checks, dispatch and bounded cleanup. */
#ifndef _PROC_H
#define _PROC_H
#include <life.h>
#include <image.h>
#include <local.h>
#include <kstack.h>
#include <abi/proc.h>
#include <abi/signal.h>
#include <observe.h>
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
/* Trusted boot/test constructor. Native create first validates its fixed Set,
   limits and credentials; raw PID construction is not exported to userspace. */
int procnew(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, Handle *out);
/* Initial init only: one-shot PID 1, parent 0. Failure after acceptance is fatal. */
int procboot(Setid set, u64 limit, u64 peak, u32 resources, Handle *out);
int proccreate(Rtable *source, Handle authority, const Createarg *args, Handle *construct,
               Handle *pid, Handle *control);
int procdestroyview(Rtable *source, Handle construct);
/* Native fixed-Life CONTROL operations. */
int procgrant(Rtable *source, Handle target, Handle resource, u64 rights, u64 offset, u64 length,
              u64 inherit, Handle *out);
int procrevoke(Rtable *source, Handle target, Handle resource);
int procbudget(Rtable *source, Handle target, const Budgetarg *args);
int procterminate(Rtable *source, Handle target);
int proccred(Rtable *source, Handle subject, Handle pid, u64 image, Cred *out);
int procgetpg(Handle pid, Handle *pgid, Handle *sid);
int procsetpg(Handle pid, Handle pgid);
int procsetsid(Handle *sid);
int procwait(Handle selector, u32 options, Liferesult *out);
int procobserve(Rtable *source, Handle permit, Handle basis, u64 relation, Handle pid, u64 number,
                Obsresult *out);
int procobsread(u64 cookie, Obsresult *out);
int procobsnext(u64 cursor, Obsresult *out, u64 *next);
int procobsdrop(u64 cookie, u32 ended);
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
int           procrunone(void);
Rtable       *proccurrent(struct Hctx *frame);
isize         procipc(u64 number, const u64 args[7], u64 outputs[8]);
int           procsiginstall(Handle pid, u32 signum, const Sigact *action);
int           procsigqueue(Handle pid, const Siginfo *info);
NORETURN void procsigreturn(u64 frame);
NORETURN void procbadsignal(u64 frame);
NORETURN void procuserreturn(struct Hctx *context);
int           procsigaction(u64 signum, u64 input, u64 output);
int           procsigsuspend(u64 mask);
int           procsigmask(u64 how, u64 mask, u64 *old);
int           procsigpending(u64 *out);
/* Typed resource commit; syscall descriptor decoding is a separate ABI step. */
int  procreplyresource(Rtable *source, Handle request, const Resreply *reply);
int  procidentity(Handle *pid, u64 *image, Handle *parent);
void procyield(void);
void procpausevm(void);
void procresumevm(void);
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
