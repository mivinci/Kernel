#include <proc.h>
#include <hal/context.h>
#include <abi/boot.h>
#include <diag.h>
#include <prep.h>

typedef struct Proc Proc;
struct Proc
{
  u64      generation;
  Handle   pid;
  Lifeid   life;
  Prepid   prep;
  Acctid   acct;
  Spaceid  space;
  Kstackid stack;
  Rtable   resources;
  Hkctx    kernel;
  Hctx    *user;
  Config   config;
  u64      image;
  u32      state, configured, phase, next;
};
STATIC_ASSERT(sizeof(Proc) <= 512, proc_budget);
static Proc  processes[PROCS];
static Local entries[PROCS][LOCALS];
static Proc *current;
static Hkctx cpu;
static u32   vm_paused;
static u32   readyhead = PG_NONE, readytail = PG_NONE;

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
  return p->state && p->pid == pid ? p : NULL;
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
  error = lifecreate(set, pid, parent, &p->life);
  if (error) {
    p->state = 0;
    return error;
  }
  error = acctcreate(set, limit, peak, &p->acct);
  if (error) {
    error = convert(error);
    goto failedlife;
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
  p->pid = pid;
  p->prep.generation = 0;
  p->prep.index = p->prep.reserved = 0;
  p->image = 1;
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
failedlife:
  if (lifecancel(p->life) || lifedetach(p->life))
    panic("process life rollback");
  p->state = 0;
  p->pid = 0;
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
  error = prepcreate(source, &p->resources, p->life, &id);
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
  out->pid = p->pid;
  out->life = p->life;
  out->acct = p->acct;
  out->space = p->space;
  out->stack = p->stack;
  out->image = p->image;
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
  p->config.entry = snapshot.entry;
  p->config.stack = snapshot.stack;
  p->config.boot = snapshot.boot;
  p->config.bootsz = snapshot.bootsz;
  p->config.sigmask = snapshot.sigmask;
  p->config.sigdefault = snapshot.sigdefault;
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
  error = validate(p, &p->config);
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
  if (p->prep.generation) {
    Prepid id = p->prep;
    prepcomplete(id);
    p->prep.generation = 0;
    if (prepdrop(id))
      panic("published preparation owner");
  }
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
  if (p->prep.generation && prepcancel(p->prep))
    panic("process preparation abort");
  return 0;
}
int
procrunone(void)
{
  Proc *p;
  if (current || vm_paused)
    return -ERR_STATE;
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
  if (spaceactivate(p->space))
    panic("process root activation");
  hal_ctx_switch(&cpu, &p->kernel);
  current = NULL;
  if (p->state != PROC_EXITING && p->state != PROC_READY)
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
    if (lifedetach(p->life))
      panic("process runtime cleanup");
    p->state = 0;
    p->pid = 0;
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
      if (procclean(p->pid) < 0)
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
  *pid = current->pid;
  *image = current->image;
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
  slot = handleindex(p->pid);
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
