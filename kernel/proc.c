#include <proc.h>
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

typedef struct Proc     Proc;
typedef union Procstage Procstage;
union Procstage
{
  Config config;
  struct
  {
    Rqid    request;
    Imageid target;
    union
    {
      Handle peer;
      u64    mask;
    } subject;
    u32 kind, options;
  } wait;
};
#define WAIT_RECV   1
#define WAIT_RESULT 2
#define WAIT_FINISH 3
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
  u32       state, configured, phase, next;
  Rquota    outgoing;
  Rmail     incoming;
  Event     events;
};
STATIC_ASSERT(sizeof(Proc) <= 512, proc_budget);
static Proc  processes[PROCS];
static Local entries[PROCS][LOCALS];
static Proc *current;
static Hkctx cpu;
static u32   vm_paused, timer_enabled;
static u64   timer_interval, timer_ticks, kernel_ticks;
static u32   timer_requested;
static u32   readyhead = PG_NONE, readytail = PG_NONE;

static Handle
pidof(const Proc *p)
{
  u32 slot = (u32) (p - processes);
  return slot == 0 && p->generation == 1 ? 1 : handlepack(HANDLE_PROCESS, p->generation, slot);
}
static void wakeone(Proc *p);
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
  hal_user_enter(current->user);
}
int
procnew(Setid set, Handle parent, u64 limit, u64 peak, u32 resources, Handle *out)
{
  Proc  *p;
  Hinit  initial;
  Handle handle, pid;
  u64    top;
  u32    slot;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (handletype(parent) != HANDLE_PROCESS || !limit || peak < limit || !resources ||
      resources > LOCALS)
    return -ERR_INVALID;
  /* Slot zero is reserved for the separately bootstrapped initial init. */
  for (slot = 1; slot < PROCS; slot++)
    if (!processes[slot].state && processes[slot].generation < HANDLE_GENERATION_MAX)
      break;
  if (slot == PROCS)
    return -ERR_CAPACITY;
  p = &processes[slot];
  p->state = PROC_BUILDING;
  p->generation++;
  pid = handlepack(HANDLE_PROCESS, p->generation, slot);
  eventinit(&p->events);
  error = lifecreate(set, pid, parent, &p->life);
  if (error) {
    p->state = 0;
    return error;
  }
  error = imagecreate(p->life, 0, &p->identity);
  if (error)
    goto failedlife;
  error = rqreserve(&p->outgoing, p->life, 0);
  if (error)
    goto failedimage;
  if (rqmailinit(&p->incoming, p->identity))
    panic("process inbox reuse");
  error = acctcreate(set, limit, peak, &p->acct);
  if (error) {
    error = convert(error);
    goto failedmail;
  }
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
    error = localallocator(&p->resources, 5, &handle);
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
  p->configured = p->phase = 0;
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
  if (localconstruct(source, id, rights, out))
    panic("preparation installation reservation");
  return 0;
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
  out->configured = p->configured;
  out->phase = p->phase;
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
  if (c->sigmask || c->sigdefault)
    return -ERR_NOTSUP;
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
  if (boot.magic != BOOT_MAGIC || boot.format != 1 || boot.bytes != c->bootsz || boot.flags ||
      boot.user_lo != 0x10000 || boot.user_hi != (1UL << 38))
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
  if (p->state != PROC_CONSTRUCT || lifestat(p->life, &life) || life.state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  if (!c || reserve > 8)
    return -ERR_INVALID;
  if (reserve)
    return -ERR_NOTSUP;
  snapshot.entry = c->entry;
  snapshot.stack = c->stack;
  snapshot.boot = c->boot;
  snapshot.bootsz = c->bootsz;
  snapshot.sigmask = c->sigmask;
  snapshot.sigdefault = c->sigdefault;
  error = validate(p, &snapshot);
  if (error)
    return error;
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
  p->configured = 1;
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
  if (p->state != PROC_CONSTRUCT || !p->configured)
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
int
procrunone(void)
{
  Proc *p;
  if (current || vm_paused)
    return -ERR_STATE;
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
  timer_requested = 0;
  if (spaceactivate(p->space))
    panic("process root activation");
  if (timer_enabled) {
    u64 ticks = hal_clock_read();
    if (ticks > ~(u64) 0 - timer_interval)
      panic("timer counter range");
    hal_timer_set(ticks + timer_interval);
    hal_user_irq(p->user, 1);
  }
  hal_ctx_switch(&cpu, &p->kernel);
  current = NULL;
  if (p->state != PROC_EXITING && p->state != PROC_READY && p->state != PROC_BLOCKED)
    panic("process CPU return state");
  return 0;
}
NORETURN void
procexit(u32 reason, u32 code, const Lifefault *fault)
{
  Proc *p = current;
  if (!p || p->state != PROC_RUNNING)
    panic("process exit source");
  if (lifeend(p->life, reason, code, fault))
    panic("process exit result");
  p->state = PROC_EXITING;
  if (imageend(p->identity, IMAGE_EXIT) || rqquotaend(&p->outgoing))
    panic("process image/request exit");
  if (rqmailclose(&p->incoming, 32) < 0)
    panic("process incoming exit");
  if (spacedeactivate())
    panic("process exit root");
  hal_ctx_switch(&p->kernel, &cpu);
  panic("ended process resumed");
}
int
procclean(Handle pid)
{
  Proc *p = find(pid);
  u32   i;
  int   error;
  if (!p)
    return -ERR_STALE;
  if (p->state == PROC_CONSTRUCT) {
    Life l;
    if (!lifestat(p->life, &l) && l.state == LIFE_ABORTED)
      p->state = PROC_EXITING;
  }
  if (p == current || p->state != PROC_EXITING)
    return -ERR_STATE;
  if (p->phase == 0) {
    if (imageend(p->identity, IMAGE_ABORT))
      panic("process closing image");
    error = rqmailclose(&p->incoming, 32);
    if (error)
      return error;
    if (p->outgoing.state == RQUOTA_OPEN && rqquotaend(&p->outgoing))
      panic("process outgoing cleanup");
    error = localclose(&p->resources);
    if (error)
      return error;
    error = localdrain(&p->resources, 32);
    if (error < 0)
      return error;
    if (!error)
      p->phase++;
  } else if (p->phase == 1) {
    error = spaceclear(p->space, 1);
    if (error < 0)
      return convert(error);
    if (!error)
      p->phase++;
  } else if (p->phase == 2) {
    for (i = 0; i < sizeof(p->kernel); i++)
      ((u8 *) &p->kernel)[i] = 0;
    if (kstackdrop(p->stack) || kstackfree(p->stack))
      panic("process stack cleanup");
    p->user = NULL;
    p->phase++;
  } else if (p->phase == 3) {
    if (spacedestroy(p->space))
      panic("process root cleanup");
    p->phase++;
  } else if (p->phase == 4) {
    if (acctclose(p->acct) || acctunbind(p->acct))
      panic("process account cleanup");
    p->phase++;
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
procwork(u32 scans)
{
  static u32 cursor;
  Proc      *p;
  Life       l;
  u32        i, worked = 0;
  if (!scans || scans > 32)
    return -ERR_INVALID;
  for (i = 0; i < scans; i++) {
    p = &processes[cursor];
    cursor = (cursor + 1) % PROCS;
    if (p == current)
      continue;
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
  if (!current || current->state != PROC_RUNNING || !vm_paused || spaceactivate(current->space))
    panic("VM resume source");
  vm_paused = 0;
}
void
procyield(void)
{
  Proc *p = current;
  u32   slot;
  if (!p || p->state != PROC_RUNNING || vm_paused)
    panic("yield source");
  slot = handleindex(pidof(p));
  p->state = PROC_READY;
  p->next = PG_NONE;
  if (readytail == PG_NONE)
    readyhead = slot;
  else
    processes[readytail].next = slot;
  readytail = slot;
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
  timer_requested = 0;
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
timerfire(void)
{
  u64 now, ticks;
  u32 i;
  int ack;
  if (!timer_enabled)
    panic("timer not enabled");
  ack = hal_timer_ack();
  if (ack < 0)
    panic("unexpected IRQ");
  if (ack) {
    timer_ticks++;
    if (clockmono(&now) || prepexpire(now, 8) < 0)
      panic("timer preparation expiry");
    for (i = 0; i < PROCS; i++) {
      Proc *p = &processes[i];
      if ((p->state == PROC_READY || p->state == PROC_RUNNING || p->state == PROC_BLOCKED) &&
          eventexpire(&p->events, now))
        wakeone(p);
    }
    ticks = hal_clock_read();
    if (ticks > ~(u64) 0 - timer_interval)
      panic("timer counter range");
    hal_timer_set(ticks + timer_interval);
  }
  return ack;
}
NORETURN void
proctimertrap(Hctx *context)
{
  if (!timer_enabled || !proccurrent(context) || vm_paused)
    panic("timer trap source");
  if (timerfire())
    procyield();
  hal_user_enter(context);
}
void
prockernelirq(void)
{
  if (timerfire()) {
    kernel_ticks++;
    if (current)
      timer_requested = 1;
  }
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
  if (timer_requested) {
    timer_requested = 0;
    procyield();
  }
}
int
procidle(void)
{
  if (current || vm_paused || !timer_enabled)
    return -ERR_STATE;
  if (readyhead != PG_NONE)
    return 0;
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
  int      wake;
  if (!p || (p->state != PROC_READY && p->state != PROC_RUNNING && p->state != PROC_BLOCKED))
    return;
  eventpost(&p->events, rqsignals(&p->outgoing, &p->incoming));
  if (p->state != PROC_BLOCKED)
    return;
  if (p->stage.wait.kind == WAIT_RECV)
    wake = p->incoming.immediate != PG_NONE || (p->events.pending & p->stage.wait.subject.mask) ||
           (!(p->stage.wait.options & RECV_EVENTS_ONLY) && !listempty(&p->incoming.queue));
  else
    wake = rqresult(p->stage.wait.request, p->identity, p->stage.wait.options, &result) !=
           -ERR_WOULD_BLOCK;
  if (wake) {
    p->stage.wait.subject.peer = 0;
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
    if (p->state != PROC_BLOCKED || p->stage.wait.kind == WAIT_RECV || !p->stage.wait.subject.peer)
      return 0;
    target.generation = p->stage.wait.target.generation;
    target.index = p->stage.wait.target.index;
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
  p->state = PROC_BLOCKED;
  if (spacedeactivate())
    panic("IPC blocking root");
  hal_ctx_switch(&p->kernel, &cpu);
  if (current != p || p->state != PROC_RUNNING)
    panic("IPC blocking continuation");
  p->stage.wait.kind = 0;
  p->stage.wait.subject.peer = 0;
}
static int
waitresult(Rqid id, u32 options, u32 block, Rqresult *out)
{
  Rqinfo  info;
  Imageid target;
  Handle  peer;
  int     error;
  for (;;) {
    error = rqresult(id, current->identity, options, out);
    if (error != -ERR_WOULD_BLOCK || !block)
      return error;
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
    current->stage.wait.target = target;
    current->stage.wait.subject.peer = peer;
    current->stage.wait.kind = options == RQ_FINISH ? WAIT_FINISH : WAIT_RESULT;
    current->stage.wait.options = options;
    sleepcurrent();
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
    current->stage.wait.kind = WAIT_RECV;
    current->stage.wait.subject.mask = mask;
    current->stage.wait.options = (u32) options;
    sleepcurrent();
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
  u32      i, kind;
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
    error = rqsubmit(&current->outgoing, &server->incoming, current->identity, (Handle) args[0],
                     RQ_NORMAL, kind, &message, (u32) args[5], &id);
    if (error)
      return error;
    wakeone(server);
    if (kind == IPC_CALL && args[5] & CALL_ASYNC) {
      outputs[2] = (u64) rqhandle(id, HANDLE_RPC);
      return 0;
    }
    if (rqhold(id))
      panic("synchronous IPC reference");
    error = waitresult(id, 0, 1, &result);
    if (error) {
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
        if (!error)
          for (i = 0; i < 4; i++)
            outputs[3 + i] = result.message.words[i];
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
    if (nr == SYS_REPLY && args[5])
      return args[5] == REPLY_RESOURCE ? -ERR_NOTSUP : -ERR_INVALID;
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
      if (!error)
        for (i = 0; i < 4; i++)
          outputs[3 + i] = result.message.words[i];
    }
    if (rqdrop(id))
      panic("IPC result wait reference release");
    return error;
  default:
    return -ERR_NOTSUP;
  }
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
  error = controlproc(source, authority, CONTROL_INSTALL, &p, NULL);
  if (error)
    return error;
  error = localnative(source, resource);
  if (error)
    return error;
  return localcopy(source, resource, &p->resources, rights, offset, length, (u32) inherit, out);
}
int
procrevoke(Rtable *source, Handle authority, Handle resource)
{
  Proc *p;
  int   error;
  error = controlproc(source, authority, CONTROL_REVOKE, &p, NULL);
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
