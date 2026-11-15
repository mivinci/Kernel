#include <prep.h>
#include <diag.h>
typedef struct Prep Prep;
struct Prep
{
  u64     generation;
  Rtable *source, *target;
  u64     sourceepoch, targetepoch;
  Handle  owner, pid;
  u64     sourceimage, image;
  Lifeid  life;
  Acctid  acct;
  Spaceid space, sourcespace;
  u32     state, refs, temporary, cursor, leased;
};
STATIC_ASSERT(PREPS > 0 && PREPS <= 192, prep_capacity);
STATIC_ASSERT(sizeof(Prep) <= 256, prep_budget);
static Prep preps[PREPS];
static Prep *
find(Prepid id)
{
  Prep *p;
  if (id.reserved || id.index >= PREPS)
    return NULL;
  p = &preps[id.index];
  return p->state && p->generation == id.generation ? p : NULL;
}
static int
sourcevalid(const Prep *p)
{
  return p->source && p->source->state == LOCAL_OPEN && p->source->epoch == p->sourceepoch &&
         p->source->owner == p->owner && p->source->image == p->sourceimage;
}
static int
active(Prep *p)
{
  Life l;
  Acct a;
  return p->state == PREP_ACTIVE && sourcevalid(p) && p->target && p->target->state == LOCAL_OPEN &&
         p->target->epoch == p->targetepoch && p->target->owner == p->pid &&
         p->target->image == p->image && !lifestat(p->life, &l) && l.state == LIFE_CONSTRUCT &&
         l.pid == p->pid && !acctstat(p->acct, &a) && a.state == AC_ACTIVE;
}
static void
fill(const Prep *p, Prepinfo *out)
{
  out->target = p->target;
  out->pid = p->pid;
  out->acct = p->acct;
  out->space = p->space;
  out->image = p->image;
  out->state = p->state;
  out->temporary = p->temporary;
  out->refs = p->refs;
}
int
prepcreate(Rtable *source, Rtable *target, Lifeid life, Prepid *out)
{
  Life  l;
  Prep *p;
  u32   i, count = 0, slot = PG_NONE;
  if (!source || !target || !out || source == target || source->state != LOCAL_OPEN ||
      target->state != LOCAL_OPEN || handletype(source->owner) != HANDLE_PROCESS ||
      !source->image || lifestat(life, &l) || l.state != LIFE_CONSTRUCT || l.pid != target->owner ||
      l.parent != source->owner || !target->image)
    return -ERR_INVALID;
  for (i = 0; i < PREPS; i++) {
    p = &preps[i];
    if (!p->state && p->generation != ~(u64) 0 && slot == PG_NONE)
      slot = i;
    if (p->state == PREP_ACTIVE && p->source == source && p->sourceepoch == source->epoch)
      count++;
    if (p->state == PREP_ACTIVE && p->pid == l.pid)
      return -ERR_STATE;
  }
  if (slot == PG_NONE || count >= 3)
    return -ERR_CAPACITY;
  if (spacehold(target->space))
    return -ERR_STALE;
  if (lifehold(life, LIFE_CLEANUP)) {
    spacedrop(target->space);
    return -ERR_STALE;
  }
  p = &preps[slot];
  p->generation++;
  p->source = source;
  p->target = target;
  p->sourceepoch = source->epoch;
  p->targetepoch = target->epoch;
  p->owner = source->owner;
  p->sourceimage = source->image;
  p->pid = l.pid;
  p->image = target->image;
  p->life = life;
  p->acct = target->acct;
  p->space = target->space;
  p->sourcespace = source->space;
  p->state = PREP_ACTIVE;
  p->refs = 1;
  p->temporary = p->cursor = 0;
  p->leased = 1;
  out->generation = p->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}
int
prepcheck(Prepid id, Rtable *source, Prepinfo *out)
{
  Prep *p = find(id);
  if (!p || p->source != source)
    return -ERR_BAD_HANDLE;
  if (!active(p))
    return -ERR_REVOKED;
  if (out)
    fill(p, out);
  return 0;
}
int
prepstat(Prepid id, Prepinfo *out)
{
  Prep *p = find(id);
  if (!p || !out)
    return -ERR_STALE;
  fill(p, out);
  return 0;
}
int
prephold(Prepid id)
{
  Prep *p = find(id);
  if (!p)
    return -ERR_STALE;
  if (p->refs == 0xffffffffU)
    return -ERR_OVERFLOW;
  p->refs++;
  return 0;
}
int
prepdrop(Prepid id)
{
  Prep *p = find(id);
  if (!p || !p->refs)
    return -ERR_STALE;
  p->refs--;
  if (!p->refs) {
    if (p->state == PREP_ACTIVE || p->temporary || p->leased)
      panic("live preparation without owner");
    if (lifedrop(p->life, LIFE_CLEANUP))
      panic("preparation life reference");
    p->state = 0;
  }
  return 0;
}
int
preppublishcheck(Prepid id)
{
  Prep *p = find(id);
  if (!p || !active(p))
    return -ERR_REVOKED;
  return p->temporary ? -ERR_BUSY : 0;
}
static void
releaseview(Prep *p)
{
  if (p->leased && spacedrop(p->space))
    panic("preparation space lease");
  p->leased = 0;
  p->target = NULL;
}
void
prepcomplete(Prepid id)
{
  Prep *p = find(id);
  if (!p || p->state != PREP_ACTIVE || p->temporary)
    panic("preparation publication");
  p->state = PREP_COMMITTED;
  releaseview(p);
}
int
prepcancel(Prepid id)
{
  Prep *p = find(id);
  Life  l;
  if (!p)
    return -ERR_STALE;
  if (p->state != PREP_ACTIVE)
    return 0;
  if (lifestat(p->life, &l))
    panic("preparation target life");
  if (l.state == LIFE_CONSTRUCT && lifecancel(p->life))
    panic("preparation cancel");
  else if (l.state != LIFE_CONSTRUCT && l.state != LIFE_ABORTED)
    return -ERR_STATE;
  p->state = PREP_ABORTED;
  releaseview(p);
  return 0;
}
int
preptempcheck(Prepid id, Spaceid source, Memid mem)
{
  Prep *p = find(id);
  Mem   m;
  if (!p || !active(p))
    return -ERR_REVOKED;
  if (p->sourcespace.index != source.index || p->sourcespace.generation != source.generation ||
      memstat(mem, &m) || m.acct.index != p->acct.index || m.acct.generation != p->acct.generation)
    return -ERR_DENIED;
  return 0;
}
int
preptemphold(Prepid id)
{
  Prep *p = find(id);
  int   error;
  if (!p || !active(p))
    return -ERR_REVOKED;
  if (p->temporary == 0xffffffffU)
    return -ERR_OVERFLOW;
  error = prephold(id);
  if (error)
    return error;
  p->temporary++;
  return 0;
}
int
preptempdrop(Prepid id)
{
  Prep *p = find(id);
  if (!p || !p->temporary)
    return -ERR_STALE;
  p->temporary--;
  return prepdrop(id);
}
int
prepclean(Prepid id, u32 slots)
{
  Prep *p = find(id);
  u32   end, more;
  if (!p)
    return -ERR_STALE;
  if (!slots || slots > 32 || p->state == PREP_ACTIVE)
    return -ERR_STATE;
  if (prephold(id))
    return -ERR_OVERFLOW;
  if (p->source && p->source->epoch == p->sourceepoch) {
    end = MIN(p->source->count, p->cursor + slots);
    localclearscope(p->source, id, p->cursor, end - p->cursor);
    p->cursor = end;
    more = p->cursor < p->source->count && p->refs > 1;
  } else
    more = 0;
  if (prepdrop(id))
    panic("preparation cleanup lease");
  return (int) more;
}
void
prepownerclose(Rtable *source)
{
  Prepid id;
  u32    i;
  for (i = 0; i < PREPS; i++)
    if (preps[i].state == PREP_ACTIVE && preps[i].source == source &&
        preps[i].sourceepoch == source->epoch) {
      id.generation = preps[i].generation;
      id.index = i;
      id.reserved = 0;
      if (prepcancel(id))
        panic("preparation source ended");
    }
}
void
prepsourceclosed(Rtable *source)
{
  u32 i;
  for (i = 0; i < PREPS; i++)
    if (preps[i].state && preps[i].source == source && preps[i].sourceepoch == source->epoch) {
      if (preps[i].state == PREP_ACTIVE)
        panic("preparation source remained active");
      preps[i].source = NULL;
    }
}

int
prepwork(u32 scans)
{
  static u32 cursor;
  Prepid     id;
  Prep      *p;
  u32        i, worked = 0;
  if (!scans || scans > 32)
    return -ERR_INVALID;
  for (i = 0; i < scans; i++) {
    p = &preps[cursor];
    id.generation = p->generation;
    id.index = cursor;
    id.reserved = 0;
    cursor = (cursor + 1) % PREPS;
    if (!p->state || p->state == PREP_ACTIVE || !p->source || p->cursor >= p->source->count)
      continue;
    if (prepclean(id, 32) < 0)
      panic("preparation CPU cleanup");
    worked++;
  }
  return (int) worked;
}
