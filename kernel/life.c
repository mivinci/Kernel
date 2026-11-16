#include <life.h>
#include <diag.h>
static Life lives[LIFES];
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
  Life *l;
  u32   i, slot = PG_NONE, run;
  int   error;
  if (!out || handletype(pid) != HANDLE_PROCESS ||
      (pid == 1 ? parent != 0 : handletype(parent) != HANDLE_PROCESS) || pid == parent)
    return -ERR_INVALID;
  run = handleindex(pid);
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
  l->generation++;
  l->pid = pid;
  l->parent = parent;
  l->sequence = l->imagegen = l->account = 0;
  l->set = settag(set);
  l->group = 0;
  l->fault.pc = l->fault.address = l->fault.cause = 0;
  l->state = LIFE_CONSTRUCT;
  l->run = run;
  l->reason = l->code = l->waitref = 0;
  l->instance = 1;
  l->controls = l->observers = l->cleanup = 0;
  l->cred.ruid = l->cred.euid = l->cred.rgid = l->cred.egid = 0;
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
  return 0;
}
int
lifeend(Lifeid id, u32 reason, u32 code, const Lifefault *fault)
{
  Life *l = find(id);
  if (!l)
    return -ERR_STALE;
  if (l->pid == 1)
    panic("init exited");
  if (l->state != LIFE_LIVE)
    return -ERR_STATE;
  if (reason < LIFE_EXIT || reason > LIFE_FAULT || code > 255 ||
      (reason != LIFE_EXIT && (!code || code > 31)) ||
      (reason == LIFE_FAULT ? !fault : fault != NULL))
    return -ERR_INVALID;
  if (l->group && groupdeactivate(l->group))
    panic("ended group membership");
  l->reason = reason;
  l->code = code;
  if (fault) {
    l->fault.pc = fault->pc;
    l->fault.address = fault->address;
    l->fault.cause = fault->cause;
  }
  l->sequence++;
  l->state = LIFE_ENDED;
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
  l->parent = newparent;
  return 0;
}
int
lifecollect(Lifeid id, Handle parent, Liferesult *out)
{
  Life *l = find(id);
  if (!out)
    return -ERR_INVALID;
  out->pid = 0;
  out->sequence = 0;
  out->reason = out->code = 0;
  if (!l)
    return -ERR_STALE;
  if (handletype(parent) != HANDLE_PROCESS || l->parent != parent)
    return -ERR_DENIED;
  if (l->state == LIFE_LIVE)
    return -ERR_WOULD_BLOCK;
  if (l->state != LIFE_ENDED || !l->waitref)
    return -ERR_STATE;
  out->pid = l->pid;
  out->sequence = l->sequence;
  out->reason = l->reason;
  out->code = l->code;
  l->waitref = 0;
  if (l->group) {
    if (groupdetach(l->group))
      panic("collected group relationship");
    l->group = 0;
  }
  leave(l);
  reap(l);
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
  out->parent = l->parent;
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
