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
  if (!l->instance)
    return;
  if (setleave(l->set))
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
  if (l->instance)
    panic("life instance retained");
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
  l->sequence = 0;
  l->set = set;
  l->fault.pc = l->fault.address = l->fault.cause = 0;
  l->state = LIFE_CONSTRUCT;
  l->run = run;
  l->reason = l->code = l->waitref = 0;
  l->instance = 1;
  l->controls = l->observers = l->cleanup = 0;
  out->generation = l->generation;
  out->index = slot;
  out->reserved = 0;
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
  out->set = l->set;
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
  return 0;
}
