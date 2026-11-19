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
/* Trusted synchronous fork builder. Caller supplies the parent's actual mail
   limit and fixed same-Set policy, freezes its source/context and masks IRQs
   with both roots inactive. No native CREATE admission, checkpoints or publish.
   Returns one owned FORK preparation; ordinary configure/start reject it.
   Failure cancels/cleans the entire unseen Proc; successful preparations use
   normal owner-death/timeout/abort cleanup. No C stack is copied. */
int procforkbuild(Setid set, Rtable *source, const struct Hctx *parent, u32 pending, u32 reserve,
                  Handle *construct, Handle *pid);
int procdestroyview(Rtable *source, Handle construct);
/* Native fixed-Life CONTROL operations. */
int procgrant(Rtable *source, Handle target, Handle resource, u64 rights, u64 offset, u64 length,
              u64 inherit, Handle *out);
int procrevoke(Rtable *source, Handle target, Handle resource);
int procbudget(Rtable *source, Handle target, const Budgetarg *args);
int procterminate(Rtable *source, Handle target);
int procttycreate(Rtable *source, Handle system, Handle control, Handle *resource, Handle *id);
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
int     procrunone(void);
Rtable *proccurrent(struct Hctx *frame);
isize   procipc(u64 number, const u64 args[7], u64 outputs[8]);
/* Trusted fixture/boot installation; raw IRQ numbers are not native authority. */
int           procmmioinstallgroup(Handle pid, u64 page, u64 members, u64 rights, Handle *out);
int           procmmioinstall(Handle pid, u32 device, u32 region, u64 rights, Handle *out);
int           procirqinstall(Handle pid, u32 number, u32 bit, Handle *out);
int           procirqbind(Rtable *source, Handle authority, Handle irq, u32 bit, Handle *out);
int           procirqack(Handle binding);
int           procirqunbind(Rtable *source, Handle authority, Handle binding);
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
int  procidentity(Handle *pid, u64 *image, Handle *parent);
void procyield(void);
void procpausevm(void);
void procresumevm(void);
/* Trusted fork continuation only. enter requires an owned abortable Space
   transaction and paused current user root. yield keeps the kernel root on
   dispatch; no transient leaf/borrow may cross it. leave returns ownership to
   the synchronous caller before commit or after abort, still VM-paused.
   Termination of a suspended scope transfers its Prep to CPU rollback first.
   No native admission, IRQ window, STOP policy or arbitrary VM work is enabled. */
int  procforkenter(Prepid prep);
void procforkyield(void);
int  procforkleave(void);
/* Completed-user-trap COW write resolution; success retries original PC. */
int proccowfault(struct Hctx *context);
u64 proccowfaults(void);
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
NORETURN void procinterrupttrap(struct Hctx *context);
#endif
