#include <life.h>
#include <diag.h>
#include <abi/signal.h>
static Life          lives[LIFES];
typedef struct Usage Usage;
struct Usage
{
  u64 user, kernel;
};
/* Kept by Life until wait consumes the result, independent of Run reuse. */
static Usage finalusage[LIFES], childusage[LIFES];

static u64
addusage(u64 a, u64 b)
{
  return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}
/* Cached run owner and monotonic death watermark make logical adoption O(1).
   Actual Proc constructors never reuse a full PID generation. */
static u16    runlife[PROCS];
static u64    retired[PROCS];
static Handle childhint[PROCS];
static Life *
runowner(Handle pid)
{
  u32 slot, entry;
  for (slot = 0; slot < PROCS; slot++) {
    entry = runlife[slot];
    if (entry && lives[entry - 1].pid == pid)
      return &lives[entry - 1];
  }
  return NULL;
}
static void
changed(Handle parent)
{
  Life *owner = runowner(parent);
  if (owner)
    childhint[owner->run] = parent;
  else if (parent == 1)
    childhint[0] = parent;
}
static Handle
parentof(const Life *l)
{
  Life *parent;
  u32   slot, entry;
  if (l->parent == 1 || l->pid == 1 ||
      (l->state != LIFE_LIVE && !(l->state == LIFE_ENDED && l->waitref)))
    return l->parent;
  slot = handleindex(l->parent);
  if (slot >= PROCS)
    return l->parent; /* Only trusted synthetic constructors may lack a parent. */
  entry = runlife[slot];
  if (entry && entry <= LIFES) {
    parent = &lives[entry - 1];
    if (parent->pid == l->parent && (parent->state == LIFE_LIVE || parent->state == LIFE_CONSTRUCT))
      return l->parent;
  }
  return handlegen(l->parent) <= retired[slot] ? 1 : l->parent;
}
static void
retire(const Life *l)
{
  u32 slot = handleindex(l->pid);
  u64 generation = handlegen(l->pid);
  if (slot < PROCS && generation > retired[slot])
    retired[slot] = generation;
}
static Life *
find(Lifeid id)
{
  Life *l;
  if (id.reserved || id.index >= LIFES)
    return NULL;
  l = &lives[id.index];
  return l->state && l->generation == id.generation ? l : NULL;
}
static void
leave(Life *l)
{
  Setid set;
  if (!l->instance)
    return;
  if (setlookup(l->set, &set) || setleave(set))
    panic("life instance accounting");
  l->instance = 0;
}
static void
reap(Life *l)
{
  if (l->run != PG_NONE || l->waitref || l->controls || l->observers || l->cleanup)
    return;
  if (l->state != LIFE_ENDED && l->state != LIFE_ABORTED)
    panic("live instance without run slot");
  if (l->instance || l->group)
    panic("life relationship retained");
  l->state = 0;
}
int
lifecreate(Setid set, Handle pid, Handle parent, Lifeid *out)
{
  return lifecreaterun(set, pid, parent, handleindex(pid), out);
}
int
lifecreaterun(Setid set, Handle pid, Handle parent, u32 run, Lifeid *out)
{
  Life *l;
  u32   i, slot = PG_NONE;
  int   error;
  if (!out || handletype(pid) != HANDLE_PROCESS ||
      (pid == 1 ? parent != 0 : handletype(parent) != HANDLE_PROCESS) || pid == parent)
    return -ERR_INVALID;
  if (run >= PROCS)
    return -ERR_INVALID;
  for (i = 0; i < LIFES; i++) {
    l = &lives[i];
    if (l->state) {
      if (l->pid == pid || l->run == run)
        return -ERR_STATE;
    } else if (slot == PG_NONE && l->generation != ~(u64) 0)
      slot = i;
  }
  if (slot == PG_NONE)
    return -ERR_CAPACITY;
  error = setenter(set);
  if (error)
    return error == AC_LIMIT ? -ERR_LIMIT : -ERR_INVALID;
  l = &lives[slot];
  finalusage[slot] = childusage[slot] = (Usage){0, 0};
  l->generation++;
  l->pid = pid;
  l->parent = parent;
  l->sequence = l->imagegen = l->account = 0;
  l->set = settag(set);
  l->group = 0;
  l->fault.pc = l->fault.address = l->fault.cause = 0;
  l->state = LIFE_CONSTRUCT;
  l->run = run;
  l->reason = l->code = l->waitref = l->child_policy = l->exit_policy = 0;
  l->stopped = 0;
  l->instance = 1;
  l->controls = l->observers = l->cleanup = 0;
  l->cred.ruid = l->cred.euid = l->cred.rgid = l->cred.egid = 0;
  runlife[run] = (u16) (slot + 1);
  out->generation = l->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}
int
lifecredentials(Lifeid id, const Cred *cred)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (!cred)
    return -ERR_INVALID;
  if (l->state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  l->cred.ruid = cred->ruid;
  l->cred.euid = cred->euid;
  l->cred.rgid = cred->rgid;
  l->cred.egid = cred->egid;
  return 0;
}
int
lifechildpolicy(Lifeid id, u32 policy)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if ((policy & ~(LIFE_AUTOCHILD | LIFE_IGNORECHLD)) ||
      ((policy & LIFE_IGNORECHLD) && !(policy & LIFE_AUTOCHILD)))
    return -ERR_INVALID;
  if (l->state != LIFE_CONSTRUCT && l->state != LIFE_LIVE)
    return -ERR_STATE;
  l->child_policy = (u8) policy;
  return 0;
}
static void
collected(Life *l, Handle parent)
{
  l->parent = parent;
  l->waitref = 0;
  if (l->group) {
    if (groupdetach(l->group))
      panic("collected group relationship");
    l->group = 0;
  }
  leave(l);
  reap(l);
}
static Life *
liveparent(Handle pid)
{
  Life *l = runowner(pid);
  return l && l->state == LIFE_LIVE ? l : NULL;
}
int
lifepublish(Lifeid id)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (l->state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  if (l->group && groupactivate(l->group))
    return -ERR_STATE;
  l->state = LIFE_LIVE;
  l->waitref = l->pid != 1;
  l->sequence++;
  return 0;
}
int
lifeorphan(Handle group, u32 *out)
{
  Group relation, parentgroup;
  Life *child, *parent;
  u32   slot, entry;
  int   error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = groupstat(group, &relation);
  if (error)
    return error;
  if (relation.kind != HANDLE_GROUP)
    return -ERR_BAD_HANDLE;
  for (slot = 0; slot < PROCS; slot++) {
    entry = runlife[slot];
    if (!entry || entry > LIFES)
      continue;
    child = &lives[entry - 1];
    if (child->state != LIFE_LIVE || child->group != group)
      continue;
    parent = liveparent(parentof(child));
    if (!parent || parent->group == group || !parent->group)
      continue;
    error = groupstat(parent->group, &parentgroup);
    if (error)
      return error;
    if (parentgroup.session == relation.session)
      return 0;
  }
  *out = 1;
  return 0;
}
int
lifecancel(Lifeid id)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (l->pid == 1)
    panic("init construction cancelled");
  if (l->state != LIFE_CONSTRUCT)
    return -ERR_STATE;
  if (l->group) {
    if (groupdetach(l->group))
      panic("canceled group reservation");
    l->group = 0;
  }
  l->state = LIFE_ABORTED;
  retire(l);
  return 0;
}
int
lifejobcheck(Lifeid id, u32 stopped)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (stopped > 1)
    return -ERR_INVALID;
  if (l->state != LIFE_LIVE)
    return -ERR_STATE;
  return l->stopped != stopped && l->sequence >= ~(u64) 0 - 1 ? -ERR_OVERFLOW : 0;
}
int
lifestop(Lifeid id, u32 signum)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (signum != SIGNAL_STOP && signum != SIGNAL_TSTP && signum != SIGNAL_TTIN &&
      signum != SIGNAL_TTOU)
    return -ERR_INVALID;
  if (l->state != LIFE_LIVE)
    return -ERR_STATE;
  if (l->stopped)
    return 0;
  /* Reserve the last version for an exit that must never fail for capacity. */
  if (l->sequence >= ~(u64) 0 - 1)
    return -ERR_OVERFLOW;
  l->stopped = 1;
  l->reason = LIFE_STOPPED;
  l->code = (u8) signum;
  l->sequence++;
  changed(parentof(l));
  return 0;
}
int
lifecontinue(Lifeid id)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (l->state != LIFE_LIVE)
    return -ERR_STATE;
  if (!l->stopped)
    return 0;
  if (l->sequence >= ~(u64) 0 - 1)
    return -ERR_OVERFLOW;
  l->stopped = 0;
  l->reason = LIFE_CONTINUED;
  l->code = 0;
  l->sequence++;
  changed(parentof(l));
  return 0;
}
int
lifeendusage(Lifeid id, u32 reason, u32 code, const Lifefault *fault, u64 user, u64 kernel)
{
  Life  *l = find(id), *parent;
  Handle owner;
  if (!l)
    return -ERR_STALE;
  if (l->pid == 1) {
    putstr("INITEXIT reason=");
    puthex(reason);
    putstr(" code=");
    puthex(code);
    putstr("\n");
    panic("init exited");
  }
  if (l->state != LIFE_LIVE)
    return -ERR_STATE;
  if (reason < LIFE_EXIT || reason > LIFE_FAULT || code > 255 ||
      (reason != LIFE_EXIT && (!code || code > 31)) ||
      (reason == LIFE_FAULT ? !fault : fault != NULL))
    return -ERR_INVALID;
  if (l->group && groupdeactivate(l->group))
    panic("ended group membership");
  if (l->sequence == ~(u64) 0)
    panic("life final version");
  finalusage[id.index].user = addusage(user, childusage[id.index].user);
  finalusage[id.index].kernel = addusage(kernel, childusage[id.index].kernel);
  l->stopped = 0;
  l->reason = reason;
  l->code = code;
  if (fault) {
    l->fault.pc = fault->pc;
    l->fault.address = fault->address;
    l->fault.cause = fault->cause;
  }
  l->sequence++;
  l->state = LIFE_ENDED;
  retire(l);
  owner = parentof(l);
  parent = liveparent(owner);
  l->exit_policy = parent ? parent->child_policy : 0;
  if (l->waitref && parent && (parent->child_policy & LIFE_AUTOCHILD))
    collected(l, owner);
  changed(owner);
  changed(1); /* A blocked init must also reconsider newly adopted children. */
  return 0;
}
int
lifeend(Lifeid id, u32 reason, u32 code, const Lifefault *fault)
{
  return lifeendusage(id, reason, code, fault, 0, 0);
}
int
lifechildusage(Lifeid id, u64 *user, u64 *kernel)
{
  if (!find(id))
    return -ERR_STALE;
  if (!user || !kernel || user == kernel)
    return -ERR_INVALID;
  *user = childusage[id.index].user;
  *kernel = childusage[id.index].kernel;
  return 0;
}
int
lifedetach(Lifeid id)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if ((l->state != LIFE_ENDED && l->state != LIFE_ABORTED) || l->run == PG_NONE)
    return -ERR_STATE;
  if (runlife[l->run] != id.index + 1)
    panic("life run owner cache");
  runlife[l->run] = 0;
  l->run = PG_NONE;
  if (l->state == LIFE_ABORTED)
    leave(l);
  reap(l);
  return 0;
}
static u32 *
counter(Life *l, u32 kind)
{
  if (kind == LIFE_CONTROL)
    return &l->controls;
  if (kind == LIFE_OBSERVER)
    return &l->observers;
  if (kind == LIFE_CLEANUP)
    return &l->cleanup;
  return NULL;
}
int
lifehold(Lifeid id, u32 kind)
{
  Life *l = find(id);
  u32  *refs;
  if (!l)
    return -ERR_STALE;
  refs = counter(l, kind);
  if (!refs)
    return -ERR_INVALID;
  if (*refs == 0xffffffffU)
    return -ERR_OVERFLOW;
  (*refs)++;
  return 0;
}
int
lifedrop(Lifeid id, u32 kind)
{
  Life *l = find(id);
  u32  *refs;
  if (!l)
    return -ERR_STALE;
  refs = counter(l, kind);
  if (!refs || !*refs)
    return -ERR_INVALID;
  (*refs)--;
  reap(l);
  return 0;
}
int
lifereparent(Lifeid id, Handle oldparent, Handle newparent)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (handletype(newparent) != HANDLE_PROCESS || newparent == l->pid)
    return -ERR_INVALID;
  if (!l->waitref || (l->state != LIFE_LIVE && l->state != LIFE_ENDED))
    return -ERR_STATE;
  if (l->parent != oldparent)
    return -ERR_DENIED;
  changed(parentof(l));
  l->parent = newparent;
  changed(parentof(l));
  return 0;
}
int
lifecollect(Lifeid id, Handle parent, Liferesult *out)
{
  Life  *l = find(id), *owner;
  Usage *usage;
  if (!out)
    return -ERR_INVALID;
  out->pid = 0;
  out->sequence = 0;
  out->reason = out->code = 0;
  if (!l)
    return -ERR_STALE;
  if (handletype(parent) != HANDLE_PROCESS || parentof(l) != parent)
    return -ERR_DENIED;
  if (l->state == LIFE_LIVE)
    return -ERR_WOULD_BLOCK;
  if (l->state != LIFE_ENDED || !l->waitref)
    return -ERR_STATE;
  out->pid = l->pid;
  out->sequence = l->sequence;
  out->reason = l->reason;
  out->code = l->code;
  owner = liveparent(parent);
  if (owner) {
    usage = &childusage[owner - lives];
    usage->user = addusage(usage->user, finalusage[id.index].user);
    usage->kernel = addusage(usage->kernel, finalusage[id.index].kernel);
  }
  collected(l, parent);
  return 0;
}
int
lifetake(Lifeid id, Handle parent, u32 options, Liferesult *out)
{
  Life *l = find(id);
  if (!out)
    return -ERR_INVALID;
  out->pid = 0;
  out->sequence = 0;
  out->reason = out->code = 0;
  if (options & ~(WAIT_WNOHANG | WAIT_WUNTRACED | WAIT_WCONTINUED))
    return -ERR_INVALID;
  if (!l)
    return -ERR_STALE;
  if (handletype(parent) != HANDLE_PROCESS || parentof(l) != parent)
    return -ERR_DENIED;
  if (l->state == LIFE_ENDED)
    return lifecollect(id, parent, out);
  if (l->state != LIFE_LIVE || !l->waitref)
    return -ERR_STATE;
  if (!((l->reason == LIFE_STOPPED && (options & WAIT_WUNTRACED)) ||
        (l->reason == LIFE_CONTINUED && (options & WAIT_WCONTINUED))))
    return -ERR_WOULD_BLOCK;
  out->pid = l->pid;
  out->sequence = l->sequence;
  out->reason = l->reason;
  out->code = l->code;
  l->reason = l->code = 0;
  return 0;
}
int
lifestat(Lifeid id, Life *out)
{
  Life *l = find(id);
  if (!l || !out)
    return -ERR_STALE;
  out->generation = l->generation;
  out->pid = l->pid;
  out->parent = parentof(l);
  out->sequence = l->sequence;
  out->imagegen = l->imagegen;
  out->account = l->account;
  out->set = l->set;
  out->group = l->group;
  out->fault.pc = l->fault.pc;
  out->fault.address = l->fault.address;
  out->fault.cause = l->fault.cause;
  out->state = l->state;
  out->run = l->run;
  out->reason = l->reason;
  out->code = l->code;
  out->waitref = l->waitref;
  out->instance = l->instance;
  out->child_policy = l->child_policy;
  out->exit_policy = l->exit_policy;
  out->stopped = l->stopped;
  out->controls = l->controls;
  out->observers = l->observers;
  out->cleanup = l->cleanup;
  out->cred.ruid = l->cred.ruid;
  out->cred.euid = l->cred.euid;
  out->cred.rgid = l->cred.rgid;
  out->cred.egid = l->cred.egid;
  return 0;
}

int
lifeimage(Lifeid id, u64 *number)
{
  Life *life = find(id);
  if (!life)
    return -ERR_STALE;
  if (!number)
    return -ERR_INVALID;
  if (life->state != LIFE_CONSTRUCT && life->state != LIFE_LIVE)
    return -ERR_STATE;
  if (life->imagegen == ~(u64) 0)
    return -ERR_OVERFLOW;
  *number = ++life->imagegen;
  return 0;
}

int
lifebindacct(Lifeid id, Acctid acct)
{
  Life *l = find(id);
  Acct  account;
  Setid set;
  u64   tag;
  u32   i;
  if (!l)
    return -ERR_STALE;
  if (l->state != LIFE_CONSTRUCT || l->account)
    return -ERR_STATE;
  if (acctstat(acct, &account) || account.state != AC_ACTIVE || !account.bound)
    return -ERR_INVALID;
  if (setlookup(l->set, &set) || account.set != set.index)
    return -ERR_DENIED;
  tag = accttag(acct);
  if (!tag)
    return -ERR_STALE;
  for (i = 0; i < LIFES; i++)
    if (lives[i].state && lives[i].account == tag)
      return -ERR_STATE;
  l->account = tag;
  return 0;
}
int
lifeacct(Lifeid id, Acctid *out)
{
  Life *l = find(id);
  if (!out)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  if (!l || acctlookup(l->account, out))
    return -ERR_STALE;
  return 0;
}

int
lifebindgroup(Lifeid id, Handle group)
{
  Life *l = find(id);
  int   error;
  if (!l)
    return -ERR_STALE;
  if (l->state != LIFE_CONSTRUCT || l->group)
    return -ERR_STATE;
  error = groupattach(group);
  if (error)
    return error;
  l->group = group;
  return 0;
}
int
lifegroup(Lifeid id, Handle *out)
{
  Life *l = find(id);
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!l || !l->group)
    return -ERR_STALE;
  *out = l->group;
  return 0;
}
int
lifesetgroup(Lifeid id, Handle group)
{
  Life *l = find(id);
  int   error;
  if (!l)
    return -ERR_STALE;
  if (!l->group || (l->state != LIFE_CONSTRUCT && l->state != LIFE_LIVE))
    return -ERR_STATE;
  if (l->group == group)
    return 0;
  error = groupattach(group);
  if (error)
    return error;
  if (l->state == LIFE_LIVE) {
    error = groupactivate(group);
    if (error) {
      if (groupdetach(group))
        panic("group move rollback");
      return error;
    }
    if (groupdeactivate(l->group))
      panic("group move old membership");
  }
  if (groupdetach(l->group))
    panic("group move old reservation");
  l->group = group;
  if (l->state == LIFE_LIVE && l->waitref)
    changed(parentof(l));
  return 0;
}
int
lifebootgroup(Lifeid id)
{
  Life  *l = find(id), *parent;
  Handle group;
  u32    i;
  int    error;
  if (!l)
    return -ERR_STALE;
  if (l->state != LIFE_CONSTRUCT || l->group)
    return -ERR_STATE;
  for (i = 0; i < LIFES; i++) {
    parent = &lives[i];
    if (parent->pid == l->parent && parent->group &&
        (parent->state == LIFE_CONSTRUCT || parent->state == LIFE_LIVE))
      return lifebindgroup(id, parent->group);
  }
  /* Only the trusted boot/test factory may use the reserved initial realm.
     Native CREATE must first establish a real current-parent relationship. */
  error = groupinitial(&group);
  if (error)
    return error;
  error = lifebindgroup(id, group);
  if (groupdrop(group))
    panic("boot group lease");
  return error;
}

int
lifefindpid(Handle pid, Lifeid *out)
{
  u32   i;
  Life *l;
  if (!out)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  if (handletype(pid) != HANDLE_PROCESS)
    return -ERR_BAD_HANDLE;
  for (i = 0; i < LIFES; i++) {
    l = &lives[i];
    if (l->pid == pid && l->group &&
        (l->state == LIFE_CONSTRUCT || l->state == LIFE_LIVE ||
         (l->state == LIFE_ENDED && l->waitref))) {
      out->generation = l->generation;
      out->index = i;
      return 0;
    }
  }
  return -ERR_STALE;
}

int
lifeselect(Handle parent, const Childsel *choice, u32 start, u32 options, Lifeid *ready,
           u32 *matched)
{
  Life *l;
  Group group;
  u32   n, index;
  if (!ready || !matched)
    return -ERR_INVALID;
  ready->generation = 0;
  ready->index = ready->reserved = *matched = 0;
  if (!choice || choice->reserved || start >= LIFES ||
      options & ~(WAIT_WNOHANG | WAIT_WUNTRACED | WAIT_WCONTINUED) ||
      handletype(parent) != HANDLE_PROCESS || choice->kind > WAIT_SELECT_GROUP ||
      (choice->kind == WAIT_SELECT_ANY && choice->value))
    return -ERR_INVALID;
  if (choice->kind == WAIT_SELECT_PID && handletype(choice->value) != HANDLE_PROCESS)
    return -ERR_BAD_HANDLE;
  if (choice->kind == WAIT_SELECT_GROUP &&
      (handletype(choice->value) != HANDLE_GROUP || groupstat(choice->value, &group)))
    return -ERR_BAD_HANDLE;
  for (n = 0; n < LIFES; n++) {
    index = (start + n) % LIFES;
    l = &lives[index];
    if (!l->waitref || (l->state != LIFE_LIVE && l->state != LIFE_ENDED) || parentof(l) != parent ||
        (choice->kind == WAIT_SELECT_PID && l->pid != choice->value) ||
        (choice->kind == WAIT_SELECT_GROUP && l->group != choice->value))
      continue;
    *matched = 1;
    if (l->state == LIFE_ENDED || (l->reason == LIFE_STOPPED && (options & WAIT_WUNTRACED)) ||
        (l->reason == LIFE_CONTINUED && (options & WAIT_WCONTINUED))) {
      ready->generation = l->generation;
      ready->index = index;
      return 0;
    }
  }
  return 0;
}
int
lifehint(Handle parent)
{
  Life *owner = runowner(parent);
  u32   slot = owner ? owner->run : 0;
  if (!owner && parent != 1)
    return 0;
  if (childhint[slot] != parent)
    return 0;
  childhint[slot] = 0;
  return 1;
}
int
lifework(u32 scans)
{
  static u32 cursor;
  Life      *l;
  u32        i, changedcount = 0;
  if (!scans || scans > 32)
    return -ERR_INVALID;
  for (i = 0; i < scans; i++) {
    l = &lives[cursor];
    cursor = (cursor + 1) % LIFES;
    if ((l->state == LIFE_LIVE || (l->state == LIFE_ENDED && l->waitref)) && l->parent != 1 &&
        parentof(l) == 1) {
      l->parent = 1;
      changed(1);
      changedcount++;
    }
  }
  return (int) changedcount;
}
int
lifecheck(void)
{
  Life *l;
  u32   i, entry;
  for (i = 0; i < LIFES; i++) {
    l = &lives[i];
    if (!l->state)
      continue;
    if (l->stopped > 1 || (l->state != LIFE_LIVE && l->stopped))
      return -ERR_STATE;
    if (l->state == LIFE_LIVE &&
        (!l->sequence || l->sequence == ~(u64) 0 ||
         (l->reason != 0 && l->reason != LIFE_STOPPED && l->reason != LIFE_CONTINUED) ||
         (l->reason == LIFE_STOPPED &&
          (!l->stopped || l->code < SIGNAL_STOP || l->code > SIGNAL_TTOU)) ||
         (l->reason != LIFE_STOPPED && l->code) || (l->reason == LIFE_CONTINUED && l->stopped)))
      return -ERR_STATE;
    if ((l->child_policy | l->exit_policy) & ~(LIFE_AUTOCHILD | LIFE_IGNORECHLD))
      return -ERR_STATE;
    if (l->run != PG_NONE &&
        (l->run >= PROCS || runlife[l->run] != i + 1 || handleindex(l->pid) != l->run))
      return -ERR_STATE;
  }
  for (i = 0; i < PROCS; i++) {
    entry = runlife[i];
    if (entry && (entry > LIFES || !lives[entry - 1].state || lives[entry - 1].run != i))
      return -ERR_STATE;
    if (retired[i] > HANDLE_GENERATION_MAX ||
        (childhint[i] && !handletype(childhint[i])))
      return -ERR_STATE;
  }
  return 0;
}
