#include <proc.h>
#include <bootresource.h>
#include <hal/context.h>
#include <abi/boot.h>
#include <diag.h>
#include <prep.h>
#include <rq.h>
#include <clock.h>
#include <hal/clock.h>
#include <hal/irq.h>
#include <abi/syscall.h>
#include <event.h>
#include <signal.h>
#include <irq.h>
#include <device.h>
#include <fork.h>
#include <terminal.h>
#define CONFIG_IMAGE  1
#define CONFIG_FORK   2
#define CONFIG_KERNEL 0x80

typedef struct Proc     Proc;
typedef union Procstage Procstage;
typedef union Procphase Procphase;
union Procphase
{
  /* EXITING uses cleanup. STOPPED uses its resumable scheduling state; RUNNING
     may carry PROC_RUNNING as a deferred stop until a safe switch boundary. */
  u8 cleanup, resume;
};
union Procstage
{
  Config config;
  struct
  {
    Prepid prep;
  } fork;
  struct
  {
    Rqid request;
    union
    {
      Imageid  image;
      Prepid   prep;
      Childsel children;
    } target;
    union
    {
      Handle peer;
      u64    mask;
    } subject;
    u32 kind, options;
  } wait;
};
#define WAIT_RECV        PROCESS_WAIT_RECV
#define WAIT_RESULT      PROCESS_WAIT_RESULT
#define WAIT_FINISH      PROCESS_WAIT_FINISH
#define WAIT_DESTROY     PROCESS_WAIT_DESTROY
#define WAIT_CHILD       PROCESS_WAIT_CHILD
#define WAIT_SIGNAL      PROCESS_WAIT_SIGNAL
#define WAIT_INTERRUPTED 0x80000000U
#define WAIT_SEND        0x40000000U
#define INTERRUPT_TIMER  1
#define INTERRUPT_READY  2
#ifndef WAIT_DEPTH
#define WAIT_DEPTH 64
#endif
STATIC_ASSERT(WAIT_DEPTH > 0 && WAIT_DEPTH <= PROCS, wait_depth);

struct Proc
{
  /* Keep the 16-byte aligned switch context first to avoid internal padding. */
  Hkctx     kernel;
  u64       generation;
  Lifeid    life;
  Imageid   identity;
  Prepid    prep;
  Acctid    acct;
  Spaceid   space;
  Kstackid  stack;
  Rtable    resources;
  Hctx     *user;
  Procstage stage;
  u64       cpu_ns;
  u32       next;
  u8        state, configured;
  Procphase phase;
  u8        wait_cursor;
  Rquota    outgoing;
  Rmail     incoming;
  Event     events;
};
STATIC_ASSERT(sizeof(Proc) <= 512, proc_budget);
STATIC_ASSERT(sizeof(Procphase) == 1, proc_phase_size);
STATIC_ASSERT(offsetof(Proc, phase) == offsetof(Proc, state) + 2, proc_phase_offset);
STATIC_ASSERT(offsetof(Proc, wait_cursor) == offsetof(Proc, phase) + 1, proc_cursor_offset);
static Proc  processes[PROCS];
static Local entries[PROCS][LOCALS];
static Proc *current;
static Hkctx cpu;
static u32   vm_paused, timer_enabled;
static u64   cow_faults;
static u64   timer_interval, timer_ticks, kernel_ticks;
static u64   dispatched;
static u32   reschedule_requested;
static u32   readyhead = PG_NONE, readytail = PG_NONE;

static Handle
pidof(const Proc *p)
{
  u32 slot = (u32) (p - processes);
  return slot == 0 && p->generation == 1 ? 1 : handlepack(HANDLE_PROCESS, p->generation, slot);
}
static void wakeone(Proc *p);
static void ready(Proc *p);
static int  terminate(Proc *p);
static Proc *
find(Handle pid)
{
  Proc *p;
  u32   slot;
  if (handletype(pid) != HANDLE_PROCESS)
    return NULL;
  slot = handleindex(pid);
  if (slot >= PROCS)
    return NULL;
  p = &processes[slot];
  return p->state && pidof(p) == pid ? p : NULL;
}
static void
park(Proc *p, u32 resume)
{
  if (resume != PROC_READY && resume != PROC_BLOCKED)
    panic("stop continuation kind");
  p->phase.resume = (u8) resume;
  p->state = PROC_STOPPED;
  p->next = PG_NONE;
}
static int
convert(int error)
{
  if (!error)
    return 0;
  if (error == AC_LIMIT)
    return -ERR_LIMIT;
  if (error == AC_FULL)
    return -ERR_CAPACITY;
  if (error == AC_BUSY)
    return -ERR_BUSY;
  if (error == AC_NOMEM)
    return -ERR_NOMEM;
  return -ERR_INVALID;
}
static NORETURN void
enter(void)
{
  if (!current || current->state != PROC_RUNNING)
    panic("process entry state");
  procuserreturn(current->user);
}
static int
newproc(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, u32 booting, Handle *out)
{
  Proc  *p, *ancestor;
  Hinit  initial;
  Handle handle, pid;
  u64    top;
  u32    slot;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if ((booting ? parent != 0 : handletype(parent) != HANDLE_PROCESS) || !limit || peak < limit ||
      !resources || resources > LOCALS)
    return -ERR_INVALID;
  if (booting) {
    if (processes[0].state || processes[0].generation)
      return -ERR_STATE;
    slot = 0;
  } else {
    for (slot = 1; slot < PROCS; slot++)
      if (!processes[slot].state && processes[slot].generation < HANDLE_GENERATION_MAX)
        break;
    if (slot == PROCS)
      return -ERR_CAPACITY;
  }
  p = &processes[slot];
  p->state = PROC_BUILDING;
  p->cpu_ns = 0;
  p->wait_cursor = 0;
  p->generation++;
  pid = booting ? 1 : handlepack(HANDLE_PROCESS, p->generation, slot);
  eventinit(&p->events);
  error = lifecreate(set, pid, parent, &p->life);
  if (error) {
    p->state = 0;
    return error;
  }
  error = imagecreate(p->life, 0, &p->identity);
  if (error)
    goto failedlife;
  error = sigreserve(p->identity);
  if (error)
    goto failedimage;
  ancestor = booting ? NULL : find(parent);
  if (ancestor && ancestor->state != PROC_EXITING) {
    error = sigbaseline(p->identity, ancestor->identity);
    if (error)
      goto failedsignals;
  }
  error = rqreserve(&p->outgoing, p->life, 0);
  if (error)
    goto failedsignals;
  if (rqmailinit(&p->incoming, p->identity))
    panic("process inbox reuse");
  error = acctcreate(set, limit, peak, &p->acct);
  if (error) {
    error = convert(error);
    goto failedmail;
  }
  error = lifebindacct(p->life, p->acct);
  if (error)
    goto failedacct;
  error = lifebootgroup(p->life);
  if (error)
    goto failedacct;
  error = spacecreate(p->acct, &p->space);
  if (error) {
    error = convert(error);
    goto failedacct;
  }
  error = kstackcreate(p->acct, &p->stack);
  if (error) {
    error = convert(error);
    goto failedspace;
  }
  error = localinit(&p->resources, entries[slot], resources, p->acct, p->space);
  if (error)
    goto failedstack;
  p->resources.owner = pid;
  p->resources.image = 1;
  error = localspace(&p->resources, 3, &handle);
  if (!error)
    error = localallocator(&p->resources, 7, &handle);
  if (error) {
    if (localclose(&p->resources))
      panic("process local rollback");
    while (localdrain(&p->resources, 32) > 0) {
    }
    goto failedstack;
  }
  if (kstacktop(p->stack, &top) || kstackhold(p->stack))
    panic("process stack reservation");
  p->user = (Hctx *) (top - sizeof(Hctx));
  initial.entry = initial.sp = initial.arg = 0;
  hal_user_init(p->user, &initial);
  hal_ctx_init(&p->kernel, (u64) p->user, enter);
  p->prep.generation = 0;
  p->prep.index = p->prep.reserved = 0;
  p->state = PROC_CONSTRUCT;
  p->configured = p->phase.cleanup = 0;
  p->next = PG_NONE;
  *out = pid;
  return 0;
failedstack:
  if (kstackfree(p->stack))
    panic("process stack rollback");
failedspace:
  if (spacedestroy(p->space))
    panic("process space rollback");
failedacct:
  if (acctclose(p->acct) || acctunbind(p->acct))
    panic("process account rollback");
failedmail:
  if (rqmailclose(&p->incoming, 32) || rqquotaend(&p->outgoing))
    panic("process request rollback");
failedsignals:
  if (sigrelease(p->identity))
    panic("process signal rollback");
failedimage:
  if (imageend(p->identity, IMAGE_ABORT) || imagedetach(p->identity))
    panic("process image rollback");
failedlife:
  if (lifecancel(p->life) || lifedetach(p->life))
    panic("process life rollback");
  p->state = 0;
  return error;
}
int
procprepare(Rtable *source, Handle target, u64 rights, Handle *out)
{
  Proc  *p = find(target);
  Prepid id;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_CONSTRUCT || p->prep.generation)
    return -ERR_STATE;
  if (!rights || rights & ~31UL)
    return -ERR_INVALID;
  error = localroom(source);
  if (error)
    return error;
  error = prepcreate(source, &p->resources, p->life, p->identity, &id);
  if (error)
    return error;
  p->prep = id;
  error = localconstruct(source, id, rights, out);
  if (error) {
    p->prep.generation = 0;
    if (prepcancel(id) || prepdrop(id))
      panic("preparation installation rollback");
    return error;
  }
  return 0;
}
int
procnew(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, Handle *out)
{
  return newproc(set, parent, limit, peak, resources, 0, out);
}
int
procboot(Setid set, u64 limit, u64 peak, u32 resources, Handle *out)
{
  int error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (processes[0].state || processes[0].generation)
    return -ERR_STATE;
  /* Invalid arguments can be corrected before acceptance. Once the run slot
     has acquired its one identity, boot failure cannot retry or reuse PID 1. */
  error = newproc(set, 0, limit, peak, resources, 1, out);
  if (error && processes[0].generation)
    panic("init construction failed");
  return error;
}
int
proccreate(Rtable *source, Handle authority, const Createarg *args, Handle *construct, Handle *pid,
           Handle *control)
{
  Createpolicy policy;
  Life         caller;
  Cred         cred;
  Acct         account;
  Proc        *child;
  Handle       target = 0, view = 0, manager = 0, inherited;
  int          error, more;
  if (!construct || !pid || !control)
    return -ERR_INVALID;
  *construct = *pid = *control = 0;
  if (!current || current->state != PROC_RUNNING || source != &current->resources || vm_paused)
    return -ERR_STATE;
  error = localcreatecheck(source, authority, args, &policy);
  if (error)
    return error;
  if (lifestat(current->life, &caller) || acctstat(source->acct, &account))
    return -ERR_STATE;
  if (!(policy.rights & CREATE_CREDENTIAL) &&
      (args->ruid != caller.cred.ruid || args->euid != caller.cred.euid ||
       args->rgid != caller.cred.rgid || args->egid != caller.cred.egid))
    return -ERR_DENIED;
  if (!(policy.rights & CREATE_SERVICE) && policy.set.index != account.set)
    return -ERR_DENIED;
  error = localrooms(source, policy.rights & CREATE_SERVICE ? 2 : 1);
  if (error)
    return error;
  procpausevm();
  error =
      procnew(policy.set, caller.pid, args->memlimit, args->exec_limit, args->resources, &target);
  procresumevm();
  if (error)
    return error;
  child = find(target);
  if (!child)
    panic("new process identity");
  cred.ruid = args->ruid;
  cred.euid = args->euid;
  cred.rgid = args->rgid;
  cred.egid = args->egid;
  error = lifecredentials(child->life, &cred);
  if (!error)
    error = rqmailconfigure(&child->incoming, args->pending);
  if (!error)
    error = localchildcreate(source, authority, &child->resources, args, &inherited);
  if (!error)
    error = procprepare(source, target, 31, &view);
  if (!error && policy.rights & CREATE_SERVICE)
    error = localcontrol(source, child->life, CONTROL_RIGHTS | LOCAL_GRANT, &manager);
  if (!error) {
    *construct = view;
    *pid = target;
    *control = manager;
    return 0;
  }
  if ((manager && localrevoke(source, manager)) || (view && localrevoke(source, view)))
    panic("create local rollback");
  if (procdestroy(target))
    panic("create instance rollback");
  /* No user-visible identity escaped and no target mappings exist yet. The
     rollback is a fixed empty-root/three-bootstrap-entry transaction. */
  procpausevm();
  while ((more = procclean(target)) > 0) {
  }
  procresumevm();
  if (more)
    panic("create cleanup rollback");
  return error;
}
int
procforkbuild(Setid set, Rtable *source, const Hctx *parent, u32 pending, u32 reserve,
              Handle *construct, Handle *pid)
{
  Imageid   sourceimage;
  Image     image;
  Life      caller;
  Acct      account;
  Acctid    bound;
  Set       group;
  Createarg limits;
  Proc     *child;
  Prepid    prep;
  Local    *slot;
  Handle    target = 0, view = 0;
  u32       i;
  int       error, finish, more;
  if (!construct || !pid || construct == pid)
    return -ERR_INVALID;
  *construct = *pid = 0;
  if (!source || source->state != LOCAL_OPEN || !parent || !pending || pending > PENDING_CALLS ||
      reserve > 8 || acctstat(source->acct, &account) || account.state != AC_ACTIVE ||
      setstat(set, &group) || set.index != account.set || source->count < 3 ||
      source->count > LOCALS)
    return -ERR_INVALID;
  if (imagefind(source->owner, source->image, &sourceimage) || imagestat(sourceimage, &image) ||
      image.state != IMAGE_ACTIVE || lifestat(image.life, &caller) || caller.state != LIFE_LIVE ||
      lifeacct(image.life, &bound) || bound.index != source->acct.index ||
      bound.generation != source->acct.generation)
    return -ERR_STATE;
  error = localroom(source);
  if (error)
    return error;
  limits.memlimit = account.limit;
  limits.exec_limit = account.exec_limit;
  limits.resources = source->count;
  limits.pending = pending;
  limits.ruid = caller.cred.ruid;
  limits.euid = caller.cred.euid;
  limits.rgid = caller.cred.rgid;
  limits.egid = caller.cred.egid;
  limits.reserved = 0;
  error = procnew(set, caller.pid, limits.memlimit, limits.exec_limit, limits.resources, &target);
  if (error)
    return error;
  child = find(target);
  if (!child)
    panic("fork constructed identity");
  error = lifecredentials(child->life, &caller.cred);
  if (!error)
    error = rqmailconfigure(&child->incoming, pending);
  if (!error)
    error = prepforkcreate(source, &child->resources, child->life, child->identity, &prep);
  if (!error) {
    child->prep = prep;
    child->configured = CONFIG_FORK;
    /* Replace bootstrap SELF entries with the parent lineage, never rebind the
       table or reset its epoch. Proc and Prep keep their independent root holds. */
    for (i = 0; i < child->resources.count; i++) {
      slot = &child->resources.slots[i];
      if (!slot->subtype)
        continue;
      if ((slot->subtype != LOCAL_SPACE && slot->subtype != LOCAL_ALLOC) ||
          localrevoke(&child->resources, handlepack(HANDLE_RESOURCE, slot->generation, i)))
        panic("fork bootstrap replacement");
    }
    error = forksnapshot(source, &child->resources, &limits, reserve, parent, child->user, prep);
    finish = prepforkfinish(prep);
    if (!error)
      error = finish;
  }
  if (!error)
    error = localconstruct(source, prep, 31, &view);
  if (!error) {
    *construct = view;
    *pid = target;
    return 0;
  }
  if (procdestroy(target))
    panic("fork construction cancellation");
  /* This trusted synchronous builder cannot checkpoint yet. No identity escaped;
     later native integration must use the bounded CPU cleanup/clone checkpoints. */
  while ((more = procclean(target)) > 0) {
  }
  if (more)
    panic("fork construction rollback");
  return error;
}

int
procinfo(Handle pid, Procinfo *out)
{
  Proc *p = find(pid);
  if (!p || !out)
    return -ERR_STALE;
  out->pid = pidof(p);
  out->life = p->life;
  out->identity.generation = p->identity.generation;
  out->identity.index = p->identity.index;
  out->identity.reserved = 0;
  out->acct = p->acct;
  out->space = p->space;
  out->stack = p->stack;
  out->image = p->resources.image;
  out->state = p->state;
  out->configured = p->configured != 0;
  out->phase = p->state == PROC_EXITING ? p->phase.cleanup : 0;
  out->resources = p->resources.count;
  return 0;
}
Rtable *
proctable(Handle pid)
{
  Proc *p = find(pid);
  Life  l;
  return p && p->state == PROC_CONSTRUCT && !lifestat(p->life, &l) && l.state == LIFE_CONSTRUCT
             ? &p->resources
             : NULL;
}
static int
range(Proc *p, u64 address, u64 bytes, u64 prot)
{
  Hpte entry;
  u64  page, end;
  if (!bytes || address < 0x10000 || address >= (1UL << 38) || bytes > (1UL << 38) - address)
    return -ERR_INVALID;
  end = address + bytes;
  for (page = ALIGN_DOWN(address, PAGE_SIZE); page < end; page += PAGE_SIZE)
    if (!spacelookup(p->space, page, &entry) || entry.prot != (prot | VM_USER) ||
        entry.attr != VM_NORMAL)
      return -ERR_FAULT;
  return 0;
}
static int
validate(Proc *p, const Config *c)
{
  Boot boot;
  Hpte guard;
  int  error;
  if (!c || (c->entry & (HAL_ENTRY_ALIGN - 1)) || (c->stack & 15) || c->stack < 0x20000 ||
      c->stack > (1UL << 38) || (c->boot & 7) || c->bootsz < sizeof(Boot) || c->bootsz > 65536)
    return -ERR_INVALID;
  if (sigmaskvalid(c->sigmask) || sigmaskvalid(c->sigdefault))
    return -ERR_INVALID;
  error = range(p, c->entry, HAL_ENTRY_ALIGN, VM_READ | VM_EXEC);
  if (error)
    return error;
  error = range(p, c->stack - 65536, 65536, VM_READ | VM_WRITE);
  if (error)
    return error;
  if (spacelookup(p->space, ALIGN_DOWN(c->stack - 65536, PAGE_SIZE) - PAGE_SIZE, &guard))
    return -ERR_FAULT;
  error = range(p, c->boot, c->bootsz, VM_READ);
  if (error)
    return error;
  if (spaceread(p->space, c->boot, &boot, sizeof(boot)))
    return -ERR_FAULT;
  if (boot.magic != BOOT_MAGIC || boot.format != BOOT_FORMAT || boot.bytes != c->bootsz ||
      boot.flags || boot.user_lo != 0x10000 || boot.user_hi != (1UL << 38))
    return -ERR_INVALID;
  if (spacebootcheck(p->space, boot.package_va, boot.package_bytes))
    return -ERR_INVALID;
  return 0;
}
int
procconfigure(Handle pid, const Config *c, u32 reserve)
{
  Proc  *p = find(pid);
  Config snapshot;
  Life   life;
  Hinit  initial;
  int    error;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_CONSTRUCT || p->configured == CONFIG_FORK || lifestat(p->life, &life) ||
      life.state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  if (!c || reserve > 8)
    return -ERR_INVALID;
  snapshot.entry = c->entry;
  snapshot.stack = c->stack;
  snapshot.boot = c->boot;
  snapshot.bootsz = c->bootsz;
  snapshot.sigmask = c->sigmask;
  snapshot.sigdefault = c->sigdefault;
  error = validate(p, &snapshot);
  if (error)
    return error;
  error = siginitialcheck(p->identity, snapshot.sigmask, snapshot.sigdefault);
  if (error)
    return error;
  error = localreserve(&p->resources, p->identity, reserve);
  if (error)
    return error;
  if (siginitial(p->identity, snapshot.sigmask, snapshot.sigdefault))
    panic("initial signal commit");
  initial.entry = snapshot.entry;
  initial.sp = snapshot.stack;
  initial.arg = snapshot.boot;
  hal_user_init(p->user, &initial);
  p->stage.config.entry = snapshot.entry;
  p->stage.config.stack = snapshot.stack;
  p->stage.config.boot = snapshot.boot;
  p->stage.config.bootsz = snapshot.bootsz;
  p->stage.config.sigmask = snapshot.sigmask;
  p->stage.config.sigdefault = snapshot.sigdefault;
  p->configured = CONFIG_IMAGE;
  return 0;
}
int
procpublish(Handle pid)
{
  Proc *p = find(pid);
  u32   slot;
  int   error;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_CONSTRUCT || p->configured != CONFIG_IMAGE)
    return -ERR_STATE;
  error = validate(p, &p->stage.config);
  if (error)
    return error;
  if (p->prep.generation) {
    error = preppublishcheck(p->prep);
    if (error)
      return error;
  }
  error = lifepublish(p->life);
  if (error)
    return error;
  if (imageactivate(p->identity))
    panic("process image publication");
  if (p->prep.generation) {
    Prepid id = p->prep;
    prepcomplete(id);
    p->prep.generation = 0;
    if (prepdrop(id))
      panic("published preparation owner");
  }
  p->stage.wait.kind = 0;
  p->stage.wait.subject.peer = 0;
  slot = handleindex(pid);
  p->state = PROC_READY;
  if (readytail == PG_NONE)
    readyhead = slot;
  else
    processes[readytail].next = slot;
  readytail = slot;
  return 0;
}
int
procdestroy(Handle pid)
{
  Proc *p = find(pid);
  Life  life;
  int   error;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_CONSTRUCT || lifestat(p->life, &life))
    return -ERR_STATE;
  if (life.state == LIFE_CONSTRUCT) {
    error = lifecancel(p->life);
    if (error)
      return error;
  } else if (life.state != LIFE_ABORTED)
    return -ERR_STATE;
  p->state = PROC_EXITING;
  if (imageend(p->identity, IMAGE_ABORT))
    panic("process image abort");
  if (p->prep.generation && prepcancel(p->prep))
    panic("process preparation abort");
  return 0;
}
static void wakechecks(void);
/* Selected-thread elapsed time includes its kernel work, excludes CPU idle and
   other threads. Single CPU, IRQs masked at dispatch and snapshot boundaries. */
static int
cputime(const Proc *p, u64 *out)
{
  u64 now, elapsed;
  int error;
  *out = p->cpu_ns;
  if (p != current)
    return 0;
  error = clockmono(&now);
  if (error)
    return error;
  if (now < dispatched)
    return -ERR_OVERFLOW;
  elapsed = now - dispatched;
  if (elapsed > ~(u64) 0 - *out)
    return -ERR_OVERFLOW;
  *out += elapsed;
  return 0;
}
int
procrunone(void)
{
  Proc *p;
  if (current || vm_paused)
    return -ERR_STATE;
  lifework(8);
  wakechecks();
  if (readyhead == PG_NONE)
    return -ERR_WOULD_BLOCK;
  p = &processes[readyhead];
  readyhead = p->next;
  if (readyhead == PG_NONE)
    readytail = PG_NONE;
  p->next = PG_NONE;
  if (p->state != PROC_READY)
    panic("process ready queue");
  current = p;
  p->state = PROC_RUNNING;
  reschedule_requested = 0;
  if (p->configured & CONFIG_KERNEL)
    vm_paused = 1;
  else if (spaceactivate(p->space))
    panic("process root activation");
  if (timer_enabled) {
    u64 ticks = hal_clock_read();
    if (ticks > ~(u64) 0 - timer_interval)
      panic("timer counter range");
    hal_timer_set(ticks + timer_interval);
  }
  hal_user_irq(p->user, 1);
  if (clockmono(&dispatched))
    panic("dispatch clock range");
  hal_ctx_switch(&cpu, &p->kernel);
  if (vm_paused)
    panic("process left paused VM on CPU");
  if (cputime(p, &p->cpu_ns))
    panic("CPU accounting range");
  current = NULL;
  if (p->state != PROC_EXITING && p->state != PROC_READY && p->state != PROC_BLOCKED &&
      p->state != PROC_STOPPED)
    panic("process CPU return state");
  return 0;
}
static void
end(Proc *p, u32 reason, u32 code, const Lifefault *fault)
{
  Life  child;
  Proc *parent;
  u64   status;
  if (lifeend(p->life, reason, code, fault))
    panic("process exit result");
  if (lifestat(p->life, &child))
    panic("committed child identity");
  parent = find(child.parent);
  if (!(child.exit_policy & LIFE_IGNORECHLD) && parent &&
      (parent->state == PROC_READY || parent->state == PROC_RUNNING ||
       parent->state == PROC_BLOCKED || parent->state == PROC_STOPPED)) {
    status = code;
    if (reason != LIFE_EXIT)
      status |= WAIT_SIGNALLED;
    if (reason == LIFE_FAULT)
      status |= WAIT_FAULT;
    if (sigchild(parent->identity, child.pid, child.cred.ruid, WAIT_EXITED, status))
      panic("committed child signal");
    wakeone(parent);
  }
  p->state = PROC_EXITING;
  p->phase.cleanup = 0;
  if (imageend(p->identity, IMAGE_EXIT) || rqquotaend(&p->outgoing) || sigdiscard(p->identity))
    panic("process image/request exit");
  /* Withdraw every unpublished child before deferred Local/VM teardown. */
  prepownerclose(&p->resources);
  if (p->configured & CONFIG_KERNEL) {
    Prepid prep = p->stage.fork.prep;
    if (prepforkabandon(prep))
      panic("terminated fork continuation handoff");
    p->configured &= ~CONFIG_KERNEL;
    p->stage.wait.kind = 0;
    if (prepdrop(prep))
      panic("terminated fork continuation reference");
  }
  if (rqmailclose(&p->incoming, 32) < 0)
    panic("process incoming exit");
  if (acctclose(p->acct))
    panic("process account exit");
}
NORETURN void
procexit(u32 reason, u32 code, const Lifefault *fault)
{
  Proc *p = current;
  if (!p || p->state != PROC_RUNNING)
    panic("process exit source");
  if (vm_paused && !(p->configured & CONFIG_KERNEL))
    panic("unowned paused VM exit");
  end(p, reason, code, fault);
  if (vm_paused)
    vm_paused = 0; /* The terminating continuation already uses the kernel root. */
  else if (spacedeactivate())
    panic("process exit root");
  hal_ctx_switch(&p->kernel, &cpu);
  panic("ended process resumed");
}
int
procclean(Handle pid)
{
  Proc *p = find(pid);
  u32   i;
  Acct  account;
  int   error;
  if (!p)
    return -ERR_STALE;
  if (p->prep.generation) {
    Prepinfo prep;
    if (prepstat(p->prep, &prep))
      panic("process preparation lifetime");
    if (prep.cloning)
      return -ERR_BUSY;
  }
  if (p->state == PROC_CONSTRUCT) {
    Life l;
    if (!lifestat(p->life, &l) && l.state == LIFE_ABORTED)
      p->state = PROC_EXITING;
  }
  if (p == current || p->state != PROC_EXITING)
    return -ERR_STATE;
  /* Exit revoked publication already. A fork transaction still borrowing this
     layout must finish/hand off before Local references or root pages drain. */
  if (p->phase.cleanup <= 3) {
    error = spacefrozen(p->space);
    if (error < 0)
      panic("process cleanup root identity");
    if (error)
      return 1;
  }
  if (p->phase.cleanup == 0) {
    if (acctstat(p->acct, &account) || (account.state == AC_ACTIVE && acctclose(p->acct)))
      panic("process account closing");
    if (imageend(p->identity, IMAGE_ABORT))
      panic("process closing image");
    error = rqmailclose(&p->incoming, 32);
    if (error)
      return error;
    if (p->outgoing.state == RQUOTA_OPEN && rqquotaend(&p->outgoing))
      panic("process outgoing cleanup");
    error = obsclose(p->identity, 32);
    if (error)
      return error;
    error = localclose(&p->resources);
    if (error)
      return error;
    error = localdrain(&p->resources, 32);
    if (error < 0)
      return error;
    if (!error)
      p->phase.cleanup++;
  } else if (p->phase.cleanup == 1) {
    error = spaceclear(p->space, 1);
    if (error < 0)
      return convert(error);
    if (!error)
      p->phase.cleanup++;
  } else if (p->phase.cleanup == 2) {
    if (sigrelease(p->identity))
      panic("process signal cleanup");
    for (i = 0; i < sizeof(p->kernel); i++)
      ((u8 *) &p->kernel)[i] = 0;
    if (kstackdrop(p->stack) || kstackfree(p->stack))
      panic("process stack cleanup");
    p->user = NULL;
    p->phase.cleanup++;
  } else if (p->phase.cleanup == 3) {
    if (spacedestroy(p->space))
      panic("process root cleanup");
    p->phase.cleanup++;
  } else if (p->phase.cleanup == 4) {
    if (acctunbind(p->acct))
      panic("process account cleanup");
    p->phase.cleanup++;
  } else {
    if (p->prep.generation) {
      Prepid id = p->prep;
      p->prep.generation = 0;
      if (prepcancel(id) || prepdrop(id))
        panic("process preparation cleanup");
    }
    if (imageend(p->identity, IMAGE_ABORT) || imagedetach(p->identity))
      panic("process image cleanup");
    if (lifedetach(p->life))
      panic("process runtime cleanup");
    p->state = 0;
    return 0;
  }
  return 1;
}

int
localconfigure(Rtable *source, Handle h, const Config *config, u32 reserve)
{
  Prepid id;
  Handle pid;
  int    error = localtarget(source, h, PREP_CONFIG, &id, &pid);
  return error ? error : procconfigure(pid, config, reserve);
}
int
localstart(Rtable *source, Handle h)
{
  Prepid id;
  Handle pid;
  int    error = localtarget(source, h, PREP_PUBLISH, &id, &pid);
  return error ? error : procpublish(pid);
}
int
localdestroy(Rtable *source, Handle h)
{
  Prepid id;
  Handle pid;
  int    error = localtarget(source, h, PREP_ABORT, &id, &pid);
  return error ? error : procdestroy(pid);
}
int
procdestroyview(Rtable *source, Handle view)
{
  Prepid prep;
  Handle pid;
  int    error;
  if (!current || current->state != PROC_RUNNING || source != &current->resources || vm_paused)
    return -ERR_STATE;
  error = localtarget(source, view, PREP_ABORT, &prep, &pid);
  if (error)
    return error;
  error = prephold(prep);
  if (error)
    return error;
  current->stage.wait.target.prep = prep;
  current->stage.wait.subject.peer = pid;
  current->stage.wait.kind = WAIT_DESTROY;
  error = procdestroy(pid);
  if (!error) {
    do {
      procpausevm();
      error = procclean(pid);
      procresumevm();
      if (error > 0)
        procyield();
    } while (error > 0);
    if (error == -ERR_STALE)
      error = 0; /* CPU cleanup already completed this exact old PID. */
    if (!error) {
      do {
        error = prepclean(prep, 32);
        if (error > 0)
          procyield();
      } while (error > 0);
    }
  }
  current->stage.wait.kind = 0;
  current->stage.wait.subject.peer = 0;
  if (prepdrop(prep))
    panic("destroy operation reference");
  return error;
}
int
procwork(u32 scans)
{
  static u32 cursor;
  Proc      *p;
  Life       l;
  u32        i, worked = 0;
  if (!scans || scans > 32)
    return -ERR_INVALID;
  lifework(scans < 8 ? scans : 8);
  for (i = 0; i < scans; i++) {
    p = &processes[cursor];
    cursor = (cursor + 1) % PROCS;
    if (p == current)
      continue;
    if (p->prep.generation) {
      Prepinfo prep;
      if (prepstat(p->prep, &prep))
        panic("process preparation work lifetime");
      if (prep.cloning)
        continue;
    }
    if (p->state == PROC_CONSTRUCT && !lifestat(p->life, &l) && l.state == LIFE_ABORTED)
      p->state = PROC_EXITING;
    if (p->state == PROC_EXITING) {
      if (procclean(pidof(p)) < 0)
        panic("process CPU cleanup");
      worked++;
    }
  }
  return (int) worked;
}

Rtable *
proccurrent(Hctx *frame)
{
  return current && current->state == PROC_RUNNING && current->user == frame ? &current->resources
                                                                             : NULL;
}
int
procidentity(Handle *pid, u64 *image, Handle *parent)
{
  Life l;
  if (!current || current->state != PROC_RUNNING || lifestat(current->life, &l))
    return -ERR_STATE;
  *pid = pidof(current);
  *image = current->resources.image;
  *parent = l.parent;
  return 0;
}
void
procpausevm(void)
{
  if (!current || current->state != PROC_RUNNING || vm_paused || spacedeactivate())
    panic("VM pause source");
  vm_paused = 1;
}
void
procresumevm(void)
{
  if (!current || current->state != PROC_RUNNING || !vm_paused ||
      current->configured & CONFIG_KERNEL || spaceactivate(current->space))
    panic("VM resume source");
  vm_paused = 0;
}

int
procforkenter(Prepid prep)
{
  Prepinfo info;
  u32      i;
  int      error;
  if (!current || current->state != PROC_RUNNING || !vm_paused ||
      current->configured & CONFIG_KERNEL || current->stage.wait.kind)
    return -ERR_STATE;
  if (prepstat(prep, &info) || !info.target)
    return -ERR_REVOKED;
  error = prepsnapshotcheck(prep, &current->resources, info.target);
  if (error)
    return error;
  if (spacesnapshotabortcheck(current->space, info.space))
    return -ERR_STATE;
  error = prephold(prep);
  if (error)
    return error;
  for (i = 0; i < sizeof(current->stage); i++)
    ((u8 *) &current->stage)[i] = 0;
  current->stage.fork.prep = prep;
  current->configured |= CONFIG_KERNEL;
  return 0;
}

void
procforkyield(void)
{
  Proc    *p = current;
  Prepinfo info;
  if (!p || p->state != PROC_RUNNING || !vm_paused || !(p->configured & CONFIG_KERNEL) ||
      prepstat(p->stage.fork.prep, &info) || spacesnapshotabortcheck(p->space, info.space))
    panic("fork kernel continuation boundary");
  if (p->phase.resume == PROC_RUNNING)
    park(p, PROC_READY);
  else
    ready(p);
  vm_paused = 0;
  hal_ctx_switch(&p->kernel, &cpu);
  if (current != p || p->state != PROC_RUNNING || !vm_paused || !(p->configured & CONFIG_KERNEL))
    panic("fork kernel continuation resume");
}

int
procforkleave(void)
{
  Prepid prep;
  if (!current || current->state != PROC_RUNNING || !vm_paused ||
      !(current->configured & CONFIG_KERNEL))
    return -ERR_STATE;
  prep = current->stage.fork.prep;
  current->configured &= ~CONFIG_KERNEL;
  current->stage.fork.prep.generation = 0;
  return prepdrop(prep);
}
int
proccowfault(Hctx *context)
{
  Hpte entry;
  int  error;
  if (!proccurrent(context) || vm_paused || !hal_user_writefault(context))
    return -ERR_STATE;
  if (!spacelookup(current->space, ALIGN_DOWN(context->tf.address, PAGE_SIZE), &entry) ||
      entry.prot != (VM_READ | VM_USER | VM_COW) || entry.attr != VM_NORMAL)
    return -ERR_FAULT;
  procpausevm();
  error = spaceresolve(current->space, context->tf.address);
  procresumevm();
  if (!error)
    cow_faults++;
  return error;
}
u64
proccowfaults(void)
{
  return cow_faults;
}
void
procyield(void)
{
  Proc *p = current;
  if (!p || p->state != PROC_RUNNING || vm_paused)
    panic("yield source");
  if (p->phase.resume == PROC_RUNNING)
    park(p, PROC_READY);
  else
    ready(p);
  if (spacedeactivate())
    panic("yield root");
  hal_ctx_switch(&p->kernel, &cpu);
  if (current != p || p->state != PROC_RUNNING)
    panic("yield continuation");
}

int
proctimerstart(void)
{
  u64 frequency = hal_clock_freq(), ticks, interval;
  if (current || vm_paused || timer_enabled)
    return -ERR_STATE;
  if (!frequency)
    return -ERR_NOTSUP;
  interval = frequency / 200 + (frequency % 200 != 0);
  ticks = hal_clock_read();
  if (ticks > ~(u64) 0 - interval)
    return -ERR_OVERFLOW;
  timer_interval = interval;
  timer_ticks = kernel_ticks = 0;
  reschedule_requested = 0;
  timer_enabled = 1;
  hal_timer_set(ticks + timer_interval);
  return 0;
}
u64
procticks(void)
{
  return timer_ticks;
}
static int
interruptfire(void)
{
  u64 now, ticks;
  u32 i;
  int ack;
  ack = timer_enabled ? hal_timer_ack() : 0;
  if (ack > 0) {
    timer_ticks++;
    if (clockmono(&now) || prepexpire(now, 8) < 0)
      panic("timer preparation expiry");
    for (i = 0; i < PROCS; i++) {
      Proc *p = &processes[i];
      if ((p->state == PROC_READY || p->state == PROC_RUNNING || p->state == PROC_BLOCKED ||
           p->state == PROC_STOPPED) &&
          eventexpire(&p->events, now))
        wakeone(p);
    }
    ticks = hal_clock_read();
    if (ticks > ~(u64) 0 - timer_interval)
      panic("timer counter range");
    hal_timer_set(ticks + timer_interval);
  }
  if (ack > 0)
    return INTERRUPT_TIMER;
  {
    u64     token = hal_irq_claim();
    Imageid target;
    Image   image;
    Proc   *owner;
    int     delivered;
    if (token) {
      delivered = irqfire(token, &target);
      if (delivered < 0)
        panic("unregistered IRQ");
      if (delivered) {
        if (imagestat(target, &image) || !(owner = find(image.pid)) ||
            owner->identity.generation != target.generation ||
            owner->identity.index != target.index)
          panic("IRQ owner identity");
        if (owner->state == PROC_STOPPED)
          wakeone(owner);
        if (owner->state == PROC_BLOCKED && owner->stage.wait.kind == WAIT_RECV &&
            (owner->events.pending & owner->stage.wait.subject.mask)) {
          owner->stage.wait.subject.peer = 0;
          ready(owner);
        }
        if (current && owner != current && owner->state == PROC_READY)
          return INTERRUPT_READY;
      }
    }
  }
  return 0;
}
NORETURN void
procinterrupttrap(Hctx *context)
{
  if (!proccurrent(context) || vm_paused)
    panic("interrupt trap source");
  if (interruptfire())
    procyield();
  procuserreturn(context);
}
void
prockernelirq(void)
{
  int fired = interruptfire();
  if (fired & INTERRUPT_TIMER)
    kernel_ticks++;
  if (fired && current)
    reschedule_requested = 1;
}
u64
prockernelticks(void)
{
  return kernel_ticks;
}
void
proccheckpoint(void)
{
  /* Caller has masked IRQs and released transient VM/commit state. */
  if (!current || current->state != PROC_RUNNING || vm_paused)
    panic("process checkpoint source");
  if (reschedule_requested) {
    reschedule_requested = 0;
    procyield();
  }
}
int
procidle(void)
{
  if (current || vm_paused)
    return -ERR_STATE;
  if (readyhead != PG_NONE)
    return 0;
  if (!timer_enabled && !irqwaiting())
    return -ERR_STATE;
  hal_idle();
  if (procwork(1) < 0 || prepwork(1) < 0)
    panic("idle cleanup");
  return 1;
}

static void
ready(Proc *p)
{
  u32 slot = handleindex(pidof(p));
  p->state = PROC_READY;
  p->next = PG_NONE;
  if (readytail == PG_NONE)
    readyhead = slot;
  else
    processes[readytail].next = slot;
  readytail = slot;
}
static void
wakeone(Proc *p)
{
  Rqresult result;
  Lifeid   child;
  u32      matched;
  int      wake, changed;
  if (!p || (p->state != PROC_READY && p->state != PROC_RUNNING && p->state != PROC_BLOCKED &&
             p->state != PROC_STOPPED))
    return;
  changed = lifehint(pidof(p));
  if (changed > 0)
    eventpost(&p->events, EVENT_CHILD);
  eventpost(&p->events, rqsignals(&p->outgoing, &p->incoming) | obssignals(p->identity));
  /* READY only schedules the suspended C continuation; no receive has taken
     its message/event yet. All signal producers share this commit boundary. */
  if (p->stage.wait.kind == WAIT_RECV && p->state != PROC_RUNNING) {
    wake = siginterruptready(p->identity);
    if (wake < 0)
      panic("receive signal identity");
    if (wake)
      p->stage.wait.options |= WAIT_INTERRUPTED;
  }
  if (p->state != PROC_BLOCKED && !(p->state == PROC_STOPPED && p->phase.resume == PROC_BLOCKED))
    return;
  if (p->stage.wait.kind == WAIT_SIGNAL) {
    wake = sigready(p->identity);
    if (wake < 0)
      panic("signal waiter identity");
  } else if (p->stage.wait.kind == WAIT_RECV) {
    wake = (p->stage.wait.options & WAIT_INTERRUPTED) || p->incoming.immediate != PG_NONE ||
           (p->events.pending & p->stage.wait.subject.mask) ||
           (!(p->stage.wait.options & RECV_EVENTS_ONLY) && !listempty(&p->incoming.queue));
  } else if (p->stage.wait.kind == WAIT_CHILD) {
    if (changed <= 0)
      return;
    if (lifeselect(pidof(p), &p->stage.wait.target.children, p->wait_cursor, p->stage.wait.options,
                   &child, &matched))
      panic("child wait selection");
    wake = child.generation || !matched;
  } else {
    wake = rqresult(p->stage.wait.request, p->identity, p->stage.wait.options & RQ_FINISH,
                    &result) != -ERR_WOULD_BLOCK;
    if (!wake) {
      wake = siginterruptready(p->identity);
      if (wake < 0)
        panic("request wait signal identity");
      if (wake) {
        /* SEND must be removed before the server can consume it. CALL keeps
           its original request and only abandons this synchronous wait. */
        if ((p->stage.wait.options & WAIT_SEND) && rqcancel(p->stage.wait.request, p->identity))
          panic("interrupted send cancellation");
        p->stage.wait.options |= WAIT_INTERRUPTED;
      }
    }
  }
  if (wake) {
    p->stage.wait.subject.peer = 0;
    if (p->state == PROC_STOPPED)
      /* Completion ends the strong wait, not the independent stop. */
      p->phase.resume = PROC_READY;
    else
      ready(p);
  }
}
static void
wakechecks(void)
{
  u32 i;
  /* Bounded CPU reconciliation also sees trusted cleanup done outside syscalls. */
  for (i = 0; i < PROCS; i++)
    wakeone(&processes[i]);
}
static Proc *
requestcaller(Handle handle)
{
  Rqid   id;
  Rqinfo info;
  Proc  *p;
  if (handletype(handle) != HANDLE_REQUEST)
    return NULL;
  id.generation = handlegen(handle);
  id.index = handleindex(handle);
  id.reserved = 0;
  if (rqstat(id, &info))
    return NULL;
  p = find(info.caller);
  return p && p->resources.image == info.callerimage ? p : NULL;
}
static int
waitedge(Imageid target, Handle *peer)
{
  Image image;
  Proc *p;
  u32   depth;
  *peer = 0;
  for (depth = 0; depth < WAIT_DEPTH; depth++) {
    if (imagestat(target, &image) || image.state != IMAGE_ACTIVE)
      return 0;
    p = find(image.pid);
    if (!p || p->identity.generation != target.generation || p->identity.index != target.index)
      return 0;
    if (!depth)
      *peer = image.pid;
    if (p == current)
      return -ERR_DEADLOCK;
    if ((p->state != PROC_BLOCKED &&
         !(p->state == PROC_STOPPED && p->phase.resume == PROC_BLOCKED)) ||
        (p->stage.wait.kind != WAIT_RESULT && p->stage.wait.kind != WAIT_FINISH) ||
        !p->stage.wait.subject.peer)
      return 0;
    target.generation = p->stage.wait.target.image.generation;
    target.index = p->stage.wait.target.image.index;
    target.reserved = 0;
  }
  return -ERR_DEPTH;
}
static void
sleepcurrent(void)
{
  Proc *p = current;
  if (!p || p->state != PROC_RUNNING || vm_paused || !p->stage.wait.kind)
    panic("IPC blocking source");
  if (p->phase.resume == PROC_RUNNING)
    park(p, PROC_BLOCKED);
  else
    p->state = PROC_BLOCKED;
  if (spacedeactivate())
    panic("IPC blocking root");
  hal_ctx_switch(&p->kernel, &cpu);
  if (current != p || p->state != PROC_RUNNING)
    panic("IPC blocking continuation");
  p->stage.wait.kind = 0;
  p->stage.wait.subject.peer = 0;
}
int
procwait(Handle selector, u32 options, Liferesult *out)
{
  Childsel choice;
  Lifeid   child;
  u32      matched;
  int      error;
  if (!current || !out || (options & ~(WAIT_WNOHANG | WAIT_WUNTRACED | WAIT_WCONTINUED)))
    return -ERR_INVALID;
  out->pid = 0;
  out->sequence = 0;
  out->reason = out->code = 0;
  choice.value = 0;
  choice.kind = WAIT_SELECT_ANY;
  choice.reserved = 0;
  if (selector == 0) {
    choice.kind = WAIT_SELECT_GROUP;
    error = lifegroup(current->life, &choice.value);
    if (error)
      return error;
  } else if (selector > 0) {
    if (handletype(selector) != HANDLE_PROCESS)
      return -ERR_BAD_HANDLE;
    choice.kind = WAIT_SELECT_PID;
    choice.value = selector;
  } else if (selector < -1) {
    if ((u64) selector == ((u64) 1 << 63))
      return -ERR_INVALID;
    if (handletype(-selector) != HANDLE_PROCESS)
      return -ERR_BAD_HANDLE;
    choice.kind = WAIT_SELECT_GROUP;
    error = groupmatch(-selector, &choice.value);
    if (error)
      return error == -ERR_STALE ? -ERR_CHILD : error;
  }
  if (choice.kind == WAIT_SELECT_GROUP && grouphold(choice.value))
    return -ERR_STATE;
  for (;;) {
    error = lifeselect(pidof(current), &choice, current->wait_cursor, options, &child, &matched);
    if (error)
      break;
    if (child.generation) {
      error = lifetake(child, pidof(current), options, out);
      if (!error)
        current->wait_cursor = (u8) ((child.index + 1) % LIFES);
      break;
    }
    if (!matched) {
      error = -ERR_CHILD;
      break;
    }
    if (options & WAIT_WNOHANG)
      break;
    current->stage.wait.target.children = choice;
    current->stage.wait.subject.peer = 0;
    current->stage.wait.kind = WAIT_CHILD;
    current->stage.wait.options = options;
    sleepcurrent();
  }
  if (choice.kind == WAIT_SELECT_GROUP && groupdrop(choice.value))
    panic("child wait group reference");
  return error;
}
static int
waitresult(Rqid id, u32 options, u32 block, Rqresult *out)
{
  Rqinfo  info;
  Imageid target;
  Handle  peer;
  int     error;
  for (;;) {
    error = rqresult(id, current->identity, options & RQ_FINISH, out);
    if (error != -ERR_WOULD_BLOCK || !block)
      return error;
    error = siginterruptready(current->identity);
    if (error)
      return error < 0 ? error : -ERR_INTERRUPTED;
    if (rqstat(id, &info))
      panic("IPC wait reference");
    /* The Rq retains the exact target Image; resolve only its original mail. */
    {
      Proc *p = find(info.server);
      if (p && p->resources.image == info.serverimage) {
        target.generation = p->identity.generation;
        target.index = p->identity.index;
        target.reserved = 0;
      } else {
        target.generation = 0;
        target.index = target.reserved = 0;
      }
    }
    error = waitedge(target, &peer);
    if (error)
      return error;
    current->stage.wait.request = id;
    current->stage.wait.target.image = target;
    current->stage.wait.subject.peer = peer;
    current->stage.wait.kind = options & RQ_FINISH ? WAIT_FINISH : WAIT_RESULT;
    current->stage.wait.options = options;
    sleepcurrent();
    if (current->stage.wait.options & WAIT_INTERRUPTED) {
      current->stage.wait.options &= ~WAIT_INTERRUPTED;
      return -ERR_INTERRUPTED;
    }
  }
}
static int
receive(u64 options, u64 mask, u64 outputs[8])
{
  Rqrecv received;
  u32    i, retired = 0;
  int    error;
  if (options & ~(RECV_NONBLOCK | RECV_EVENTS_ONLY) || (options & RECV_EVENTS_ONLY && !mask))
    return -ERR_INVALID;
  for (;;) {
    u32 choice;
    if (current->incoming.immediate != PG_NONE)
      return -ERR_BUSY;
    eventpost(&current->events, obssignals(current->identity));
    choice = eventchoose(&current->events, mask, !listempty(&current->incoming.queue),
                         (u32) (options & RECV_EVENTS_ONLY));
    if (choice == 1) {
      outputs[0] = IPC_EVENT;
      eventtake(&current->events, mask, &outputs[3], &outputs[4]);
      eventserved(&current->events, 1, (u32) (options & RECV_EVENTS_ONLY));
      return 0;
    }
    error = choice == 2 ? rqrecv(&current->incoming, &received) : -ERR_WOULD_BLOCK;
    if (error == 1) {
      if (++retired == 32) {
        retired = 0;
        procyield();
      }
      continue;
    }
    if (!error) {
      outputs[0] = received.kind;
      outputs[1] = (u64) received.source;
      outputs[2] = (u64) received.request;
      for (i = 0; i < 4; i++)
        outputs[3 + i] = received.message.words[i];
      outputs[7] = received.image;
      eventserved(&current->events, 2, 0);
      return 0;
    }
    if (error != -ERR_WOULD_BLOCK || options & RECV_NONBLOCK)
      return error;
    error = siginterruptready(current->identity);
    if (error)
      return error < 0 ? error : -ERR_INTERRUPTED;
    current->stage.wait.kind = WAIT_RECV;
    current->stage.wait.subject.mask = mask;
    current->stage.wait.options = (u32) options;
    sleepcurrent();
    if (current->stage.wait.options & WAIT_INTERRUPTED) {
      current->stage.wait.options &= ~WAIT_INTERRUPTED;
      return -ERR_INTERRUPTED;
    }
  }
}
isize
procipc(u64 nr, const u64 args[7], u64 outputs[8])
{
  Proc    *server, *caller;
  Image    image;
  Imageid  target;
  Rqid     id;
  Rqinfo   info;
  Rqresult result;
  Msg      message;
  u64      bits;
  u32      i, kind, context;
  Sigstat  signal;
  int      error;
  if (!current || current->state != PROC_RUNNING || vm_paused)
    panic("native IPC source");
  if (nr == SYS_CALL &&
      args[5] & ~((u64) (CALL_ASYNC | CALL_RECV_MEM | CALL_RECV_CALL | CALL_CONTROL)))
    return -ERR_INVALID;
  if (nr == SYS_RQACK && args[1] & ~((u64) (ACK_RESULT | ACK_RELEASE)))
    return -ERR_INVALID;
  if (nr == SYS_REPLY_RECV) {
    if (args[5] & ~((u64) (RECV_NONBLOCK | RECV_EVENTS_ONLY)) ||
        (args[5] & RECV_EVENTS_ONLY && !args[6]))
      return -ERR_INVALID;
  }
  for (i = 0; i < 4; i++)
    message.words[i] = args[1 + i];
  switch (nr) {
  case SYS_NOTIFY:
    if (!args[1])
      return -ERR_INVALID;
    error = localchannel(&current->resources, (Handle) args[0], LOCAL_NOTIFY, &target, &bits);
    if (error)
      return error;
    if (args[1] & ~bits || args[1] & ~EVENT_USER)
      return -ERR_DENIED;
    if (imagestat(target, &image))
      return -ERR_PEER;
    server = find(image.pid);
    if (!server || server->identity.generation != target.generation ||
        server->identity.index != target.index || server->state == PROC_EXITING)
      return -ERR_PEER;
    eventpost(&server->events, args[1]);
    wakeone(server);
    return 0;
  case SYS_ALARMSET:
  case SYS_ALARMCANCEL:
    if (!timer_enabled)
      return -ERR_NOTSUP;
    if (nr == SYS_ALARMCANCEL)
      return eventcancel(&current->events, &outputs[0]);
    if (clockmono(&bits))
      return -ERR_OVERFLOW;
    return eventalarm(&current->events, args[0], bits, &outputs[0]);
  case SYS_CALL:
  case SYS_SEND:
    kind = nr == SYS_CALL ? IPC_CALL : IPC_SEND;
    error = localchannel(&current->resources, (Handle) args[0],
                         kind == IPC_CALL ? LOCAL_CALL : LOCAL_SEND, &target, &bits);
    if (error)
      return error;
    if (imagestat(target, &image))
      return -ERR_PEER;
    server = find(image.pid);
    if (!server || server->identity.generation != target.generation ||
        server->identity.index != target.index || server->state == PROC_EXITING)
      return -ERR_PEER;
    if (sigstat(current->identity, &signal))
      panic("IPC signal budget");
    if (kind == IPC_SEND || !(args[5] & CALL_ASYNC)) {
      error = siginterruptready(current->identity);
      if (error)
        return error < 0 ? error : -ERR_INTERRUPTED;
    }
    context = signal.depth == 0 ? RQ_NORMAL : signal.depth == 1 ? RQ_SIGNAL1 : RQ_SIGNAL2;
    error = rqsubmitto(&current->outgoing, &server->incoming, current->identity, (Handle) args[0],
                       context, kind, &message, (u32) args[5], &id, &current->resources);
    if (error)
      return error;
    wakeone(server);
    if (kind == IPC_CALL && args[5] & CALL_ASYNC) {
      outputs[2] = (u64) rqhandle(id, HANDLE_RPC);
      return 0;
    }
    if (rqhold(id))
      panic("synchronous IPC reference");
    error = waitresult(id, kind == IPC_SEND ? WAIT_SEND : 0, 1, &result);
    if (error == -ERR_INTERRUPTED && kind == IPC_CALL)
      outputs[2] = (u64) rqhandle(id, HANDLE_RPC);
    else if (error) {
      /* No dispatch precedes the first strong-edge check; rejection is queued. */
      if (rqcancel(id, current->identity) || rqack(id, current->identity, 3))
        panic("rejected synchronous wait rollback");
    } else {
      error = (int) result.status;
      if (rqstat(id, &info))
        panic("synchronous IPC result reference");
      if (kind == IPC_CALL && error && !(info.flags & RQ_FINISHED))
        outputs[2] = (u64) rqhandle(id, HANDLE_RPC);
      else {
        if (!error) {
          outputs[0] = (u64) rqresultkind(id);
          for (i = 0; i < 4; i++)
            outputs[3 + i] = result.message.words[i];
        }
        if (rqack(id, current->identity, 3))
          panic("synchronous IPC consumption");
      }
    }
    if (rqdrop(id))
      panic("synchronous IPC reference release");
    return error;
  case SYS_RECV:
    return receive(args[5], args[1], outputs);
  case SYS_REPLY:
  case SYS_REPLY_RECV:
    if (nr == SYS_REPLY && args[5]) {
      Resreply descriptor;
      if (args[5] != REPLY_RESOURCE || args[2] || args[3] || args[4] || args[6] ||
          !IS_ALIGNED(args[1], 8) || args[1] < BOOT_USER_LO || args[1] >= BOOT_USER_HI ||
          sizeof(descriptor) > BOOT_USER_HI - args[1])
        return -ERR_INVALID;
      /* spaceread prechecks the complete ordinary readable user extent before
         making one snapshot. No borrowed user pointer reaches the transaction. */
      error = spaceread(current->space, args[1], &descriptor, sizeof(descriptor));
      if (error)
        return error == SP_BUSY ? -ERR_BUSY : -ERR_FAULT;
      caller = requestcaller((Handle) args[0]);
      error =
          rqreplyresource(&current->incoming, &current->resources, (Handle) args[0], &descriptor);
      if (!error)
        wakeone(caller);
      return error;
    }
    caller = requestcaller((Handle) args[0]);
    error = rqreply(&current->incoming, (Handle) args[0], &message);
    if (!error)
      wakeone(caller);
    if (error || nr == SYS_REPLY)
      return error;
    return receive(args[5], args[6], outputs);
  case SYS_DEFER:
    return rqdefer(&current->incoming, (Handle) args[0]);
  case SYS_RQFINISH:
    caller = requestcaller((Handle) args[0]);
    error = rqfinish(&current->incoming, (Handle) args[0]);
    if (!error)
      wakeone(caller);
    return error;
  case SYS_RQCHECK:
    error = rqcheck(&current->incoming, (Handle) args[0]);
    if (error > 0) {
      outputs[0] = (u64) error;
      return 0;
    }
    return error;
  case SYS_RQPOLL:
  case SYS_RQWAIT:
  case SYS_RQCANCEL:
  case SYS_RQACK:
    error = rqlookup((Handle) args[0], current->identity, &id);
    if (error)
      return error;
    if (nr == SYS_RQCANCEL) {
      if (rqstat(id, &info))
        panic("request cancellation reference");
      error = rqcancel(id, current->identity);
      if (!error) {
        wakeone(current);
        server = find(info.server);
        if (server && server->resources.image == info.serverimage)
          wakeone(server);
      }
      return error;
    }
    if (nr == SYS_RQACK)
      return rqack(id, current->identity, (u32) args[1]);
    if (args[1] & ~((u64) RQ_FINISH))
      return -ERR_INVALID;
    if (rqhold(id))
      panic("IPC result wait reference");
    error = waitresult(id, (u32) args[1], nr == SYS_RQWAIT, &result);
    if (!error) {
      error = (int) result.status;
      if (!error) {
        outputs[0] = args[1] & RQ_FINISH ? 0 : (u64) rqresultkind(id);
        for (i = 0; i < 4; i++)
          outputs[3 + i] = result.message.words[i];
      }
    }
    if (rqdrop(id))
      panic("IPC result wait reference release");
    return error;
  default:
    return -ERR_NOTSUP;
  }
}

static int
mmiobacking(Proc *target, u32 window, u64 rights, Handle *out)
{
  Memid memory;
  Mem   info;
  int   error;
  error = memdevice(target->acct, window, &memory);
  if (error)
    return convert(error);
  if (memstat(memory, &info))
    panic("MMIO installation backing");
  error = localmem(&target->resources, memory, rights, 0, info.bytes, 0, out);
  if (memfree(memory))
    panic("MMIO installation reference");
  return error;
}
int
procmmioinstallgroup(Handle pid, u64 page, u64 members, u64 rights, Handle *out)
{
  Proc *target = find(pid);
  u32   window;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!target)
    return -ERR_STALE;
  if (target->state != PROC_CONSTRUCT && target->state != PROC_READY &&
      target->state != PROC_RUNNING && target->state != PROC_BLOCKED &&
      target->state != PROC_STOPPED)
    return -ERR_STATE;
  if (!(rights & VM_READ) || rights & ~(VM_READ | VM_WRITE | LOCAL_GRANT))
    return -ERR_DENIED;
  error = localroom(&target->resources);
  if (error)
    return error;
  error = devgroup(page, members, &window);
  return error ? (error == DEV_DENIED ? -ERR_DENIED : -ERR_INVALID)
               : mmiobacking(target, window, rights, out);
}
int
procmmioinstall(Handle pid, u32 device, u32 region, u64 rights, Handle *out)
{
  Proc *target = find(pid);
  u32   window;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!target)
    return -ERR_STALE;
  if (target->state != PROC_CONSTRUCT && target->state != PROC_READY &&
      target->state != PROC_RUNNING && target->state != PROC_BLOCKED &&
      target->state != PROC_STOPPED)
    return -ERR_STATE;
  if (!(rights & VM_READ) || rights & ~(VM_READ | VM_WRITE | LOCAL_GRANT))
    return -ERR_DENIED;
  error = localroom(&target->resources);
  if (error)
    return error;
  error = devwindow(device, region, &window);
  if (error)
    return error == DEV_DENIED ? -ERR_DENIED : -ERR_BAD_HANDLE;
  return mmiobacking(target, window, rights, out);
}
int
procirqinstall(Handle pid, u32 number, u32 bit, Handle *out)
{
  Proc *target = find(pid);
  u32   source;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!target)
    return -ERR_STALE;
  if (target->state != PROC_CONSTRUCT && target->state != PROC_READY &&
      target->state != PROC_RUNNING && target->state != PROC_BLOCKED &&
      target->state != PROC_STOPPED)
    return -ERR_STATE;
  error = irqsource(number, &source);
  return error ? error
               : localirqbind(&target->resources, target->identity, &target->events, source, bit,
                              out);
}
int
procirqack(Handle binding)
{
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  return localirqack(&current->resources, binding, current->identity);
}
int
procsiginstall(Handle pid, u32 signum, const Sigact *action)
{
  Proc *p = find(pid);
  if (!p || !action)
    return -ERR_BAD_HANDLE;
  if (p->state != PROC_READY && p->state != PROC_RUNNING)
    return -ERR_BUSY;
  if (action->kind == SIGNAL_HANDLER &&
      (range(p, action->handler, HAL_ENTRY_ALIGN, VM_READ | VM_EXEC) ||
       range(p, action->trampoline, HAL_ENTRY_ALIGN, VM_READ | VM_EXEC)))
    return -ERR_FAULT;
  return siginstall(p->identity, signum, action);
}
int
procsigqueue(Handle pid, const Siginfo *info)
{
  Proc   *p = find(pid);
  Sigstat state;
  Sigact  action;
  u64     bit;
  u32     stop;
  int     error = sigvalidinfo(info);
  if (error)
    return error;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_READY && p->state != PROC_RUNNING && p->state != PROC_BLOCKED &&
      p->state != PROC_STOPPED)
    return -ERR_BUSY;
  error = siggetaction(p->identity, info->signum, &action);
  if (error)
    return error;
  if (sigstat(p->identity, &state))
    return -ERR_STALE;
  if (info->signum == SIGNAL_KILL)
    return terminate(p);
  bit = (u64) 1 << (info->signum - 1);
  stop = sigstopkind(info->signum) && action.kind == SIGNAL_DEFAULT && !(state.mask & bit);
  /* Pure jobs and blocked/ignored notifications do not abandon a C wait.
     IPC and suspend waits unwind for caught signals; parent and construction
     waits still need their own result/reference commit handling. */
  if ((p->state == PROC_BLOCKED || (p->state == PROC_STOPPED && p->phase.resume == PROC_BLOCKED)) &&
      p->stage.wait.kind != WAIT_SIGNAL && !stop && info->signum != SIGNAL_CONT &&
      p->stage.wait.kind != WAIT_RECV && p->stage.wait.kind != WAIT_RESULT &&
      p->stage.wait.kind != WAIT_FINISH && !(state.mask & bit) &&
      !sigignored(info->signum, &action))
    return -ERR_BUSY;
  error = sigqueue(p->identity, info);
  if (error)
    return error;
  if (info->signum == SIGNAL_CONT && proccontinue(pid))
    panic("signal continue preflight");
  if (stop) {
    if (procstop(pid, info->signum) || sigcancel(p->identity, bit))
      panic("signal stop preflight");
  }
  wakeone(p);
  return 0;
}
NORETURN void
procbadsignal(u64 frame)
{
  Lifefault fault;
  if (!current || current->state != PROC_RUNNING)
    panic("bad signal source");
  fault.pc = current->user->tf.pc;
  fault.address = frame;
  fault.cause = SIGNAL_BAD_FRAME_CAUSE;
  procexit(LIFE_FAULT, SIGNAL_SEGV, &fault);
}
NORETURN void
procuserreturn(Hctx *context)
{
  int signal;
  if (!proccurrent(context) || vm_paused || current->stage.wait.kind)
    panic("user return boundary");
  for (;;) {
    proccheckpoint();
    signal = sigdeliver(current->identity, current->space, context);
    if (signal < 0)
      procbadsignal(hal_user_sp(context));
    if (signal & SIG_DELIVER_STOP) {
      if (procstop(pidof(current), (u32) signal & ~SIG_DELIVER_STOP))
        panic("default stop admission");
      continue;
    }
    if (signal > 0)
      procexit(LIFE_SIGNAL, (u32) signal, NULL);
    break;
  }
  hal_user_enter(context);
}
NORETURN void
procsigreturn(u64 frame)
{
  if (!current || current->state != PROC_RUNNING)
    panic("signal return source");
  if (sigrestore(current->identity, current->space, current->user, frame))
    procbadsignal(frame);
  procuserreturn(current->user);
}
int
procsigsuspend(u64 mask)
{
  u64 original;
  int error;
  if (!current || current->state != PROC_RUNNING || vm_paused || current->stage.wait.kind)
    return -ERR_STATE;
  error = sigsetmask(current->identity, SIGNAL_SETMASK, mask, &original);
  if (error)
    return error;
  for (;;) {
    error = sigwaitreturn(current->identity, original);
    if (error > 0 && (error & SIG_DELIVER_STOP)) {
      if (procstop(pidof(current), (u32) error & ~SIG_DELIVER_STOP))
        panic("suspend stop admission");
      current->stage.wait.kind = WAIT_SIGNAL;
      current->stage.wait.subject.peer = 0;
      current->stage.wait.options = 0;
      sleepcurrent();
      continue;
    }
    if (!error)
      return -ERR_INTERRUPTED;
    if (error != -ERR_WOULD_BLOCK)
      panic("signal suspend state");
    current->stage.wait.kind = WAIT_SIGNAL;
    current->stage.wait.subject.peer = 0;
    current->stage.wait.options = 0;
    sleepcurrent();
  }
}
int
procsigaction(u64 signum, u64 input, u64 output)
{
  Sigact action, old;
  int    error;
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (signum > SIGNAL_COUNT)
    return -ERR_INVALID;
  error = siggetaction(current->identity, (u32) signum, &old);
  if (error)
    return error;
  if (input) {
    if (spaceread(current->space, input, &action, sizeof(action)))
      return -ERR_FAULT;
    error = sigactioncheck(current->identity, (u32) signum, &action);
    if (error)
      return error;
    if (action.kind == SIGNAL_HANDLER &&
        (range(current, action.handler, HAL_ENTRY_ALIGN, VM_READ | VM_EXEC) ||
         range(current, action.trampoline, HAL_ENTRY_ALIGN, VM_READ | VM_EXEC)))
      return -ERR_FAULT;
  }
  /* Output is checked/written before the action/policy commit. Input was
     already snapshotted, including when both user buffers alias. */
  if (output && spacewrite(current->space, output, &old, sizeof(old)))
    return -ERR_FAULT;
  if (input && sigsetaction(current->identity, (u32) signum, &action))
    panic("signal action commit");
  return 0;
}
int
procsigmask(u64 how, u64 mask, u64 *old)
{
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (how > SIGNAL_SETMASK)
    return -ERR_INVALID;
  return sigsetmask(current->identity, (u32) how, mask, old);
}
int
procsigpending(u64 *out)
{
  return current && current->state == PROC_RUNNING ? siggetpending(current->identity, out)
                                                   : -ERR_STATE;
}
/* IRQs remain masked through resolution and publication. No borrowed target
   table or authority may cross a checkpoint in these bounded transactions. */
static int
controlproc(Rtable *source, Handle authority, u64 rights, Proc **out, u64 *granted)
{
  Lifeid identity;
  Life   life;
  Proc  *p;
  u64    available;
  int    error;
  error = localnative(source, authority);
  if (error)
    return error;
  error = localcontrolled(source, authority, rights, &identity, &available);
  if (error)
    return error;
  if (lifestat(identity, &life))
    return -ERR_STALE;
  if (life.state != LIFE_CONSTRUCT && life.state != LIFE_LIVE)
    return -ERR_STALE;
  p = find(life.pid);
  if (!p || p->life.generation != identity.generation || p->life.index != identity.index ||
      p->state == PROC_EXITING || p->resources.state != LOCAL_OPEN)
    return -ERR_STALE;
  *out = p;
  if (granted)
    *granted = available;
  return 0;
}
int
procttycreate(Rtable *source, Handle system, Handle control, Handle *resource, Handle *id)
{
  Imageid owner;
  Proc   *target;
  Handle  terminal;
  int     error;
  if (!resource || !id)
    return -ERR_INVALID;
  *resource = *id = 0;
  error = localsystemaccess(source, system, SYSTEM_POLICY, &owner);
  if (error)
    return error;
  error = controlproc(source, control, CONTROL_CONNECT, &target, NULL);
  if (error)
    return error;
  error = localrooms(source, 1);
  if (error)
    return error;
  /* Fix the exact current Image while IRQs are masked; CONTROL's Life binding
     must never make an existing terminal follow a later exec. */
  error = terminalnew(target->identity, &terminal);
  if (error)
    return error;
  error =
      localterminal(source, terminal, TERMINAL_RIGHTS | LOCAL_GRANT, TERMINAL_SIGNALS, 0, resource);
  if (terminaldrop(terminal))
    panic("terminal creation reference");
  if (!error)
    *id = terminal;
  return error;
}
int
procttybind(Rtable *source, Handle authority, Handle sid)
{
  Handle terminal, session;
  Group  relation;
  u64    signals;
  int    error;
  error = localterminalaccess(source, authority, TERMINAL_BIND, &terminal, &signals);
  if (error)
    return error;
  if (handletype(sid) != HANDLE_PROCESS)
    return -ERR_BAD_HANDLE;
  error = sessionfind(sid, &session);
  if (error)
    return error;
  if (groupstat(session, &relation))
    return -ERR_STALE;
  if (!relation.members)
    return -ERR_STATE;
  /* BIND is administrative: the caller need not belong to this session. */
  return terminalbind(terminal, session);
}
int
procgetfg(Rtable *source, Handle authority, Handle *pgid, Handle *sid, u64 *epoch)
{
  Handle   terminal, foreground = 0, session = 0;
  Terminal snapshot;
  Group    relation;
  u64      signals;
  int      error;
  if (!pgid || !sid || !epoch)
    return -ERR_INVALID;
  *pgid = *sid = 0;
  *epoch = 0;
  error = localterminalaccess(source, authority, TERMINAL_QUERY, &terminal, &signals);
  if (error)
    return error;
  error = terminalstat(terminal, &snapshot);
  if (error)
    return error;
  if (snapshot.session) {
    if (groupstat(snapshot.session, &relation))
      return -ERR_STALE;
    session = relation.id;
  }
  if (snapshot.foreground) {
    if (groupstat(snapshot.foreground, &relation))
      return -ERR_STALE;
    foreground = relation.id;
  }
  *pgid = foreground;
  *sid = session;
  *epoch = snapshot.epoch;
  return 0;
}
int
procirqbind(Rtable *source, Handle authority, Handle irq, u32 bit, Handle *out)
{
  Proc *target;
  u32   index;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = controlproc(source, authority, CONTROL_IRQ, &target, NULL);
  if (error)
    return error;
  error = localirqresolve(source, irq, &index);
  return error
             ? error
             : localirqbind(&target->resources, target->identity, &target->events, index, bit, out);
}
int
procirqunbind(Rtable *source, Handle authority, Handle binding)
{
  Proc *target;
  int   error = controlproc(source, authority, CONTROL_IRQ, &target, NULL);
  return error ? error : localirqunbind(&target->resources, binding, target->identity);
}
static int
prepareproc(Rtable *source, Handle authority, u64 rights, Proc **out)
{
  Prepid prep;
  Handle pid;
  Proc  *p;
  int    error = localtarget(source, authority, rights, &prep, &pid);
  if (error)
    return error;
  p = find(pid);
  if (!p || p->state != PROC_CONSTRUCT || p->prep.generation != prep.generation ||
      p->prep.index != prep.index || p->resources.state != LOCAL_OPEN)
    return -ERR_STALE;
  *out = p;
  return 0;
}
static int
targetproc(Rtable *source, Handle authority, u64 rights, Proc **out)
{
  int error = prepareproc(source, authority, PREP_CONFIG, out);
  return error == -ERR_BAD_HANDLE ? controlproc(source, authority, rights, out, NULL) : error;
}
int
localconnect(Rtable *source, Handle authority, u64 kind, u64 rights, u64 bits, Handle *out)
{
  Proc *p;
  u64   available;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (kind != CONNECT_CALL && kind != CONNECT_SEND && kind != CONNECT_NOTIFY)
    return -ERR_INVALID;
  if (!(rights & LOCAL_USE) || rights & ~(LOCAL_USE | LOCAL_GRANT) ||
      (kind == CONNECT_NOTIFY ? (!bits || bits & ~EVENT_USER) : bits != 0))
    return -ERR_INVALID;
  error = prepareproc(source, authority, 0, &p);
  if (!error) {
    if (rights & LOCAL_GRANT)
      return -ERR_DENIED;
    return localendpoint(source, p->identity, (u32) kind, rights, bits, 0, out);
  }
  if (error != -ERR_BAD_HANDLE)
    return error;
  error = controlproc(source, authority, CONTROL_CONNECT, &p, &available);
  if (error)
    return error;
  if ((rights & LOCAL_GRANT) && !(available & LOCAL_GRANT))
    return -ERR_DENIED;
  return localendpoint(source, p->identity, (u32) kind, rights, bits, 0, out);
}
int
procgrant(Rtable *source, Handle authority, Handle resource, u64 rights, u64 offset, u64 length,
          u64 inherit, Handle *out)
{
  Proc *p;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (inherit & ~3UL)
    return -ERR_INVALID;
  error = targetproc(source, authority, CONTROL_INSTALL, &p);
  if (error)
    return error;
  error = localnative(source, resource);
  if (error)
    return error;
  if (source->slots[handleindex(resource)].subtype == LOCAL_TERMINAL) {
    error = prepareproc(source, authority, PREP_CONFIG, &p);
    if (error)
      return error;
  }
  return localcopy(source, resource, &p->resources, rights, offset, length, (u32) inherit, out);
}
int
procrevoke(Rtable *source, Handle authority, Handle resource)
{
  Proc *p;
  int   error;
  error = targetproc(source, authority, CONTROL_REVOKE, &p);
  if (error)
    return error;
  error = localnative(&p->resources, resource);
  if (error)
    return error;
  error = localrevoke(&p->resources, resource);
  if (error)
    return error;
  /* Twelve prepaid slots bound this scan; ACCEPTED requests keep their reply
     right. The full old Handle distinguishes independently installed copies. */
  if (rqrevoke(&p->outgoing, p->identity, resource) < 0)
    panic("process revoke quota");
  wakeone(p);
  return 0;
}

int
procbudget(Rtable *source, Handle authority, const Budgetarg *args)
{
  Proc *p;
  int   error;
  if (!args || args->max_instances || args->reserved || args->exec_limit < args->memlimit)
    return -ERR_INVALID;
  error = controlproc(source, authority, CONTROL_BUDGET, &p, NULL);
  if (error)
    return error;
  return convert(acctbudget(p->acct, args->memlimit, args->exec_limit));
}

static void
unready(Proc *p)
{
  u32 slot = readyhead, previous = PG_NONE, count;
  for (count = 0; count < PROCS && slot != PG_NONE; count++) {
    if (slot >= PROCS)
      panic("process ready index");
    if (&processes[slot] == p) {
      if (previous == PG_NONE)
        readyhead = p->next;
      else
        processes[previous].next = p->next;
      if (readytail == slot)
        readytail = previous;
      p->next = PG_NONE;
      return;
    }
    previous = slot;
    slot = processes[slot].next;
  }
  panic("process ready membership");
}
static void
jobnotice(Proc *p, u32 stopped, u32 signum)
{
  Life  child;
  Proc *parent;
  if (lifestat(p->life, &child))
    panic("job child identity");
  parent = find(child.parent);
  if (parent && (parent->state == PROC_READY || parent->state == PROC_RUNNING ||
                 parent->state == PROC_BLOCKED || parent->state == PROC_STOPPED)) {
    if (sigchild(parent->identity, child.pid, child.cred.ruid,
                 stopped ? WAIT_STOPPED : WAIT_CONTINUED, stopped ? signum : 0))
      panic("committed job child signal");
    wakeone(parent);
  }
}
int
procstop(Handle pid, u32 signum)
{
  Proc *p = find(pid);
  Life  before;
  u32   resume;
  int   error;
  if (!p)
    return -ERR_STALE;
  if ((p->state != PROC_READY && p->state != PROC_BLOCKED && p->state != PROC_RUNNING &&
       p->state != PROC_STOPPED) ||
      (p->state == PROC_RUNNING && p != current))
    return -ERR_STATE;
  if (lifestat(p->life, &before))
    return -ERR_STALE;
  error = lifestop(p->life, signum);
  if (error)
    return error;
  if (sigcancel(p->identity, (u64) 1 << (SIGNAL_CONT - 1)))
    panic("stop pending continue");
  if (!before.stopped)
    jobnotice(p, 1, signum);
  if (p->state == PROC_STOPPED)
    return 0;
  if (p == current) {
    p->phase.resume = PROC_RUNNING;
    reschedule_requested = 1;
    return 0;
  }
  resume = p->state;
  if (resume == PROC_READY)
    unready(p);
  park(p, resume);
  return 0;
}
int
proccontinue(Handle pid)
{
  Proc *p = find(pid);
  Life  before;
  u32   resume;
  int   error;
  if (!p)
    return -ERR_STALE;
  if (p->state != PROC_READY && p->state != PROC_BLOCKED && p->state != PROC_RUNNING &&
      p->state != PROC_STOPPED)
    return -ERR_STATE;
  resume = p->state == PROC_STOPPED ? p->phase.resume : 0;
  if (p->state == PROC_STOPPED && resume != PROC_READY && resume != PROC_BLOCKED)
    panic("stopped Run continuation");
  if (lifestat(p->life, &before))
    return -ERR_STALE;
  error = lifecontinue(p->life);
  if (error)
    return error;
  if (sigcancel(p->identity, SIG_STOPS))
    panic("continue pending stops");
  if (before.stopped)
    jobnotice(p, 0, 0);
  if (p->state == PROC_STOPPED) {
    p->phase.cleanup = 0;
    if (resume == PROC_READY)
      ready(p);
    else {
      p->state = PROC_BLOCKED;
      wakeone(p);
    }
  } else if (p->state == PROC_RUNNING && p->phase.resume == PROC_RUNNING)
    p->phase.cleanup = 0;
  return 0;
}
/* A READY waiter can still own its saved C frame's Rq reference. All current
   switching paths are commit checkpoints or sleepcurrent; no other transient
   VM/allocation reference may cross them. Termination discards that frame. */
int
procterminate(Rtable *source, Handle authority)
{
  Lifeid identity;
  Life   life;
  Proc  *p;
  u64    available;
  int    error;
  error = localnative(source, authority);
  if (error)
    return error;
  error = localcontrolled(source, authority, CONTROL_TERMINATE, &identity, &available);
  if (error)
    return error;
  if (lifestat(identity, &life))
    return -ERR_STALE;
  if (life.state == LIFE_ENDED)
    return 0;
  if (life.state != LIFE_LIVE)
    return -ERR_STATE;
  p = find(life.pid);
  if (!p || p->life.generation != identity.generation || p->life.index != identity.index)
    return -ERR_STALE;
  return terminate(p);
}
static int
terminate(Proc *p)
{
  if (p == current)
    procexit(LIFE_SIGNAL, SIGNAL_KILL, NULL);
  if (p->state == PROC_READY)
    unready(p);
  else if (p->state != PROC_BLOCKED && p->state != PROC_STOPPED)
    return -ERR_STATE;
  end(p, LIFE_SIGNAL, SIGNAL_KILL, NULL);
  /* Outgoing death records are terminal before releasing the abandoned waiter.
     This can reclaim a QUEUED/replied Rq, but never fabricates server finish. */
  if ((p->stage.wait.kind == WAIT_RESULT || p->stage.wait.kind == WAIT_FINISH) &&
      rqdrop(p->stage.wait.request))
    panic("terminated wait reference");
  if (p->stage.wait.kind == WAIT_DESTROY && prepdrop(p->stage.wait.target.prep))
    panic("terminated destroy reference");
  if (p->stage.wait.kind == WAIT_CHILD && p->stage.wait.target.children.kind == WAIT_SELECT_GROUP &&
      groupdrop(p->stage.wait.target.children.value))
    panic("terminated child wait reference");
  p->stage.wait.kind = 0;
  p->stage.wait.subject.peer = 0;
  return 0;
}

int
proccred(Rtable *source, Handle subject, Handle pid, u64 number, Cred *out)
{
  Lifeid  identity;
  Life    life;
  Imageid id, related;
  Image   image;
  u64     rights;
  int     error;
  if (!out || !current || source != &current->resources || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (!subject) {
    if (pid || number)
      return -ERR_INVALID;
    identity = current->life;
  } else if (handletype(subject) == HANDLE_REQUEST) {
    error = rqsource(&current->incoming, subject, &id);
    if (error)
      return error;
    if (imagestat(id, &image))
      return -ERR_STALE;
    if (pid || number) {
      if (!pid)
        return -ERR_INVALID;
      error = preprelated(image.pid, image.number, pid, number, &related);
      if (error)
        return error;
      if (imagestat(related, &image))
        return -ERR_STALE;
    }
    identity = image.life;
  } else {
    error = localcontrolled(source, subject, CONTROL_QUERY, &identity, &rights);
    if (error)
      return error;
    if (lifestat(identity, &life))
      return -ERR_STALE;
    if (pid && pid != life.pid)
      return -ERR_INVALID;
    if (number) {
      error = imagelookup(identity, number, &id);
      if (error)
        return error;
    }
  }
  if (lifestat(identity, &life))
    return -ERR_STALE;
  out->ruid = life.cred.ruid;
  out->euid = life.cred.euid;
  out->rgid = life.cred.rgid;
  out->egid = life.cred.egid;
  return 0;
}

int
procobserve(Rtable *source, Handle permit, Handle basis, u64 relation, Handle pid, u64 number,
            Obsresult *out)
{
  Imageid target, caller;
  Image   image;
  Lifeid  life;
  Life    record;
  u64     rights;
  int     error;
  if (!out || !current || source != &current->resources || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (relation > OBS_MONITOR)
    return -ERR_INVALID;
  error = localobserver(source, permit, (u32) relation);
  if (error)
    return error;
  if (relation == OBS_MONITOR) {
    error = localcontrolled(source, basis, CONTROL_QUERY, &life, &rights);
    if (error)
      return error;
    if (lifestat(life, &record))
      return -ERR_STALE;
    if (pid && pid != record.pid)
      return -ERR_INVALID;
    error = number ? imagelookup(life, number, &target) : imagecurrent(life, &target);
  } else {
    if (handletype(basis) != HANDLE_REQUEST)
      return -ERR_BAD_HANDLE;
    error = rqsource(&current->incoming, basis, &caller);
    if (error)
      return error;
    if (imagestat(caller, &image))
      return -ERR_STALE;
    if (relation == OBS_SOURCE) {
      if (pid || number)
        return -ERR_INVALID;
      target = caller;
      error = 0;
    } else {
      if (!pid || (relation == OBS_CHILD && pid == image.pid) ||
          (relation == OBS_FUTURE && (pid != image.pid || !number)))
        return -ERR_INVALID;
      error = preprelated(image.pid, image.number, pid, number, &target);
    }
  }
  if (error)
    return error;
  return obsregister(current->identity, target, out);
}
int
procobsread(u64 cookie, Obsresult *out)
{
  if (!current)
    return -ERR_STATE;
  return obsread(current->identity, cookie, out);
}
int
procobsnext(u64 cursor, Obsresult *out, u64 *next)
{
  if (!current)
    return -ERR_STATE;
  return obsnext(current->identity, cursor, out, next);
}
int
procobsdrop(u64 cookie, u32 ended)
{
  if (!current)
    return -ERR_STATE;
  return obsdrop(current->identity, cookie, ended);
}

static void
querybase(Queryrec *record, u64 kind, Handle pid, u64 stamp)
{
  u32 i;
  for (i = 0; i < sizeof(*record); i++)
    ((u8 *) record)[i] = 0;
  record->type = kind;
  record->pid = pid;
  record->stamp = stamp;
}
static int
procstate(const Proc *p)
{
  switch (p->state) {
  case PROC_CONSTRUCT:
    return PROCESS_CONSTRUCT;
  case PROC_READY:
    return PROCESS_READY;
  case PROC_RUNNING:
    return PROCESS_RUNNING;
  case PROC_BLOCKED:
    return PROCESS_BLOCKED;
  case PROC_EXITING:
    return PROCESS_EXITING;
  case PROC_STOPPED:
    return PROCESS_STOPPED;
  default:
    return 0;
  }
}
static int
querystat(Proc *p, const Life *life, Queryrec *record)
{
  Space  space;
  Acct   account;
  Handle pgid = 0, sid = 0;
  u64    usage;
  u32    i, requests;
  int    error;
  error = cputime(p, &usage);
  if (error)
    return error;
  if (life->group && groupnames(life->group, &pgid, &sid))
    return -ERR_STATE;
  querybase(record, QUERY_PROC, life->pid, p->generation);
  record->image = p->resources.image;
  record->data[0] = (u64) life->parent;
  record->data[1] = (u64) pgid;
  record->data[2] = (u64) sid;
  record->data[3] = (u64) procstate(p);
  if ((p->state == PROC_BLOCKED || (p->state == PROC_STOPPED && p->phase.resume == PROC_BLOCKED)) &&
      (p->stage.wait.kind == WAIT_RESULT || p->stage.wait.kind == WAIT_FINISH))
    record->data[4] = (u64) p->stage.wait.subject.peer;
  record->data[5] = usage;
  if (!acctstat(p->acct, &account)) {
    record->data[6] = account.used;
    record->data[7] = account.reserved;
  }
  if (p->resources.state != LOCAL_DEAD)
    for (i = 0; i < p->resources.count; i++)
      if (p->resources.slots[i].subtype && p->resources.slots[i].subtype < LOCAL_FLOOR)
        record->data[8]++;
  if (!spacestat(p->space, &space))
    record->data[9] = space.mapcount;
  error = rqcount(life->pid, p->resources.image, &requests);
  if (error)
    return error;
  record->data[10] = requests;
  if (p->state == PROC_READY || p->state == PROC_RUNNING || p->state == PROC_BLOCKED ||
      p->state == PROC_STOPPED) {
    record->data[12] = p->stage.wait.kind;
    if (p->stage.wait.kind == WAIT_CHILD) {
      record->data[13] = p->stage.wait.target.children.kind;
      if (p->stage.wait.target.children.kind == WAIT_SELECT_GROUP) {
        if (groupnames(p->stage.wait.target.children.value, &pgid, &sid))
          return -ERR_STATE;
        record->data[14] = (u64) pgid;
      } else
        record->data[14] = (u64) p->stage.wait.target.children.value;
    }
  }
  return 0;
}
int
procquery(Rtable *source, Handle authority, u64 kind, u64 cursor, u64 address, u64 count,
          u64 *written, u64 *next)
{
  Queryrec records[QUERY_RECORDS];
  Rtable  *target = source;
  Proc    *p;
  Lifeid   identity;
  Life     life;
  Acctid   accountid;
  Acct     account;
  u64      rights, following = 0;
  u32      n = 0;
  int      error;
  if (!written || !next)
    return -ERR_INVALID;
  *written = *next = 0;
  if (!current || source != &current->resources || current->state != PROC_RUNNING || vm_paused)
    return -ERR_STATE;
  if (!count || count > QUERY_RECORDS || kind < QUERY_PROC || kind > QUERY_BOOT_RESOURCE ||
      cursor >> 59 || (cursor && !(cursor >> 16)))
    return -ERR_INVALID;
  if (kind == QUERY_BOOT_RESOURCE) {
    error = bootresourcequery(source, authority, cursor, records, (u32) count, &n, &following);
    if (error)
      return error;
    error = range(current, address, count * sizeof(Queryrec), VM_READ | VM_WRITE);
    if (error)
      return error;
    if (n) {
      error = spacewrite(source->space, address, records, (u64) n * sizeof(Queryrec));
      if (error)
        return error == SP_BUSY ? -ERR_BUSY : -ERR_FAULT;
    }
    *written = n;
    *next = following;
    return 0;
  }
  identity = current->life;
  if (authority) {
    error = localqueryprep(source, authority, &target);
    if (!error) {
      if (kind != QUERY_RESOURCE)
        return -ERR_DENIED;
    } else {
      if (error != -ERR_BAD_HANDLE)
        return error;
      error = localcontrolled(source, authority, CONTROL_QUERY, &identity, &rights);
      if (error)
        return error;
      if (lifestat(identity, &life))
        return -ERR_STALE;
      p = find(life.pid);
      if (p && p->life.generation == identity.generation && p->life.index == identity.index)
        target = p->resources.state == LOCAL_DEAD ? NULL : &p->resources;
      else if (life.state == LIFE_ENDED || life.state == LIFE_ABORTED)
        target = NULL;
      else
        return -ERR_STALE;
    }
  }

  /* Validate the entire requested output range, even when fewer rows exist.
     IRQs stay masked and no VM/table borrow crosses a checkpoint. */
  error = range(current, address, count * sizeof(Queryrec), VM_READ | VM_WRITE);
  if (error)
    return error;
  if (kind == QUERY_RQ) {
    if (lifestat(identity, &life))
      return -ERR_STALE;
    error = rqquery(life.pid, authority ? 0 : source->image, cursor, records, (u32) count, &n,
                    &following);
    if (error)
      return error;
  } else if (kind == QUERY_RESOURCE) {
    if (target) {
      error = localquery(target, cursor, records, (u32) count, &n, &following);
      if (error)
        return error;
    } else if (cursor)
      return -ERR_STALE;
  } else {
    if (cursor)
      return -ERR_STALE;
    if (lifestat(identity, &life))
      return -ERR_STALE;
    if (kind == QUERY_PROC) {
      p = find(life.pid);
      if (p && p->life.generation == identity.generation && p->life.index == identity.index &&
          procstate(p)) {
        error = querystat(p, &life, &records[0]);
        if (error)
          return error;
        n = 1;
      }
    } else if (kind == QUERY_LIFE) {
      querybase(&records[0], kind, life.pid, life.sequence);
      records[0].data[0] = (u64) life.parent;
      records[0].data[1] = life.state == LIFE_ENDED ? life.reason : 0;
      records[0].data[2] = life.state == LIFE_ENDED ? life.code : 0;
      records[0].data[3] = life.waitref;
      records[0].data[4] = life.observers;
      records[0].data[5] = life.cleanup;
      n = 1;
    } else if (!lifeacct(identity, &accountid)) {
      if (acctstat(accountid, &account))
        return -ERR_STALE;
      querybase(&records[0], kind, life.pid, account.generation);
      records[0].data[0] = account.set;
      records[0].data[1] = account.limit;
      records[0].data[2] = account.exec_limit;
      records[0].data[3] = account.used;
      records[0].data[4] = account.reserved;
      records[0].data[5] = account.state;
      n = 1;
    }
  }
  if (n) {
    error = spacewrite(source->space, address, records, (u64) n * sizeof(Queryrec));
    if (error)
      return error == SP_BUSY ? -ERR_BUSY : -ERR_FAULT;
  }
  *written = n;
  *next = following;
  return 0;
}

static int
jobrecord(Handle pid, Lifeid *identity, Life *life, Group *group)
{
  Proc *p;
  int   error = lifefindpid(pid, identity);
  if (error)
    return error;
  if (lifestat(*identity, life))
    return -ERR_STALE;
  if (life->state == LIFE_CONSTRUCT) {
    p = find(pid);
    if (!p || p->state != PROC_CONSTRUCT)
      return -ERR_STALE;
  }
  return groupstat(life->group, group);
}
int
procgetpg(Handle pid, Handle *pgid, Handle *sid)
{
  Lifeid identity;
  Life   life, source;
  Group  group, own;
  int    error;
  if (!pgid || !sid)
    return -ERR_INVALID;
  *pgid = *sid = 0;
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (pid < 0)
    return -ERR_INVALID;
  if (!pid)
    pid = pidof(current);
  error = jobrecord(pid, &identity, &life, &group);
  if (error)
    return error;
  if (lifestat(current->life, &source) || groupstat(source.group, &own))
    return -ERR_STATE;
  if (own.session != group.session)
    return -ERR_DENIED;
  return groupnames(life.group, pgid, sid);
}
int
procsetpg(Handle pid, Handle pgid)
{
  Lifeid identity;
  Life   life, source;
  Group  old, own;
  Handle candidate = 0, targetpg, targetsid;
  Proc  *p;
  int    error;
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  if (pid < 0 || pgid < 0)
    return -ERR_INVALID;
  if (!pid)
    pid = pidof(current);
  if (!pgid)
    pgid = pid;
  if (handletype(pgid) != HANDLE_PROCESS)
    return -ERR_BAD_HANDLE;
  error = jobrecord(pid, &identity, &life, &old);
  if (error)
    return error;
  if (life.state != LIFE_CONSTRUCT && life.state != LIFE_LIVE)
    return -ERR_STALE;
  if (pid != pidof(current)) {
    if (life.parent != pidof(current))
      return -ERR_DENIED;
    p = find(pid);
    if (!p || p->state == PROC_BUILDING || p->state == PROC_EXITING)
      return -ERR_STALE;
    if (p->resources.image != 1)
      return -ERR_STATE;
  }
  if (lifestat(current->life, &source) || groupstat(source.group, &own) ||
      groupnames(life.group, &targetpg, &targetsid))
    return -ERR_STATE;
  if (old.session != own.session || targetsid == pid)
    return -ERR_DENIED;
  error = groupfind(pgid, own.session, &candidate);
  if (!error)
    error = grouphold(candidate);
  else if (error == -ERR_STALE && pgid == pid)
    error = groupcreate(pgid, own.session, &candidate);
  else if (error == -ERR_STALE)
    return -ERR_DENIED;
  if (error)
    return error;
  error = lifesetgroup(identity, candidate);
  if (groupdrop(candidate))
    panic("setpg candidate lease");
  return error;
}
int
procsetsid(Handle *sid)
{
  Handle pid, session = 0, group = 0;
  int    error;
  if (!sid)
    return -ERR_INVALID;
  *sid = 0;
  if (!current || current->state != PROC_RUNNING)
    return -ERR_STATE;
  pid = pidof(current);
  if (groupidused(pid))
    return -ERR_DENIED;
  error = sessioncreate(pid, &session);
  if (error)
    return error == -ERR_STATE ? -ERR_DENIED : error;
  error = groupcreate(pid, session, &group);
  if (!error) {
    error = lifesetgroup(current->life, group);
    if (groupdrop(group))
      panic("setsid group lease");
  }
  if (groupdrop(session))
    panic("setsid session lease");
  if (!error)
    *sid = pid;
  return error;
}
