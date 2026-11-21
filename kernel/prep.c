#include <prep.h>
#include <diag.h>
#include <clock.h>
typedef struct Prep Prep;
struct Prep
{
  u64     generation, deadline;
  Rtable *source, *target;
  u64     sourceepoch, targetepoch;
  Handle  owner, pid;
  u64     sourceimage, image;
  Lifeid  life;
  Imageid identity;
  Acctid  acct;
  Spaceid space, sourcespace;
  u32     state, refs, temporary, cursor, leased, expired, origin, cloning;
  union
  {
    Spacetxn tree;
    struct
    {
      Imageid source;
      u64     ticket;
    } exec;
  } work;
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
static void
expire(Prep *p, u64 now)
{
  Prepid id;
  if (p->state != PREP_ACTIVE || now < p->deadline)
    return;
  id.generation = p->generation;
  id.index = (u32) (p - preps);
  id.reserved = 0;
  if (imageend(p->identity, IMAGE_TIMEOUT) || prepcancel(id))
    panic("preparation timeout cancellation");
  p->expired = 1;
}
static int
imageactive(Prep *p)
{
  Image image;
  return !imagestat(p->work.exec.source, &image) && image.state == IMAGE_ACTIVE &&
         acctexecvalid(p->acct, p->work.exec.ticket);
}
static int
active(Prep *p)
{
  Life  l;
  Acct  a;
  Image image;
  u64   now;
  if (clockmono(&now))
    panic("preparation clock range");
  expire(p, now);
  return p->state == PREP_ACTIVE && !imagestat(p->identity, &image) &&
         image.state == IMAGE_PREPARING && sourcevalid(p) && p->target &&
         p->target->state == LOCAL_OPEN && p->target->epoch == p->targetepoch &&
         p->target->owner == p->pid && p->target->image == p->image && !lifestat(p->life, &l) &&
         l.pid == p->pid && !acctstat(p->acct, &a) && a.state == AC_ACTIVE &&
         (p->origin == PREP_EXEC ? l.state == LIFE_LIVE && imageactive(p)
                                 : l.state == LIFE_CONSTRUCT);
}
static void
fill(const Prep *p, Prepinfo *out)
{
  out->target = p->state == PREP_ACTIVE ? p->target : NULL;
  out->pid = p->pid;
  out->acct = p->acct;
  out->space = p->space;
  out->image = p->image;
  out->state = p->state;
  out->temporary = p->temporary;
  out->refs = p->refs;
  out->expired = p->expired;
  out->origin = p->origin;
  out->cloning = p->cloning;
  out->deadline = p->deadline;
  out->identity.generation = p->identity.generation;
  out->identity.index = p->identity.index;
  out->identity.reserved = 0;
}
static int
create(Rtable *source, Rtable *target, Lifeid life, Imageid identity, u32 origin, u64 ticket,
       Prepid *out)
{
  Life    l;
  Image   image, running;
  Imageid sourceid;
  Acctid  acct;
  Prep   *p;
  u32     i, count = 0, slot = PG_NONE;
  u64     now;
  int     error;
  if (!source || !target || !out || source == target || source->state != LOCAL_OPEN ||
      target->state != LOCAL_OPEN || handletype(source->owner) != HANDLE_PROCESS ||
      !source->image || lifestat(life, &l) || l.pid != target->owner || !target->image)
    return -ERR_INVALID;
  if (origin == PREP_EXEC) {
    if (l.state != LIFE_LIVE || l.pid != source->owner || target->image <= source->image ||
        source->slots == target->slots ||
        (source->space.index == target->space.index &&
         source->space.generation == target->space.generation) ||
        lifeacct(life, &acct) || accttag(source->acct) != accttag(acct) ||
        accttag(target->acct) != accttag(acct) || !acctexecvalid(acct, ticket) ||
        imagelookup(life, source->image, &sourceid) || imagestat(sourceid, &running) ||
        running.state != IMAGE_ACTIVE)
      return -ERR_INVALID;
  } else if (l.state != LIFE_CONSTRUCT || l.parent != source->owner)
    return -ERR_INVALID;
  if (imagestat(identity, &image) || image.life.generation != life.generation ||
      image.life.index != life.index || image.pid != target->owner ||
      image.number != target->image || image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  error = clockmono(&now);
  if (error)
    return error;
  if (now > ~(u64) 0 - PREP_NS)
    return -ERR_OVERFLOW;
  for (i = 0; i < PREPS; i++) {
    p = &preps[i];
    if (!p->state && p->generation != ~(u64) 0 && slot == PG_NONE)
      slot = i;
    if (p->state == PREP_ACTIVE && p->source == source && p->sourceepoch == source->epoch)
      count++;
    if (p->state == PREP_ACTIVE && p->pid == l.pid)
      return origin == PREP_EXEC ? -ERR_BUSY : -ERR_STATE;
  }
  if (slot == PG_NONE || count >= 3)
    return -ERR_CAPACITY;
  if (spacehold(target->space))
    return -ERR_STALE;
  if (imagehold(identity, IMAGE_CLEANUP)) {
    spacedrop(target->space);
    return -ERR_STALE;
  }
  error = imageorigin(identity, source->image);
  if (error) {
    imagedrop(identity, IMAGE_CLEANUP);
    spacedrop(target->space);
    return error;
  }
  p = &preps[slot];
  p->identity.generation = identity.generation;
  p->identity.index = identity.index;
  p->identity.reserved = 0;
  p->generation++;
  p->deadline = now + PREP_NS;
  p->expired = 0;
  p->origin = origin;
  p->cloning = origin == PREP_FORK ? PREP_CLONING : 0;
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
  if (origin == PREP_EXEC) {
    p->work.exec.source = sourceid;
    p->work.exec.ticket = ticket;
  } else
    p->work.tree.phase = 0;
  out->generation = p->generation;
  out->index = slot;
  out->reserved = 0;
  return 0;
}
int
prepcreate(Rtable *source, Rtable *target, Lifeid life, Imageid identity, Prepid *out)
{
  return create(source, target, life, identity, PREP_CREATE, 0, out);
}
int
prepforkcreate(Rtable *source, Rtable *target, Lifeid life, Imageid identity, Prepid *out)
{
  return create(source, target, life, identity, PREP_FORK, 0, out);
}
int
prepexecopen(Rtable *source, Rtable *target, Lifeid life, Imageid identity, u64 ticket, Prepid *out)
{
  return create(source, target, life, identity, PREP_EXEC, ticket, out);
}
int
prepexecticket(Prepid id, Rtable *source, u64 *out)
{
  Prep *p = find(id);
  int   error = prepcheck(id, source, NULL);
  if (error)
    return error;
  if (!out || p->origin != PREP_EXEC)
    return -ERR_STATE;
  *out = p->work.exec.ticket;
  return 0;
}
int
prepexecbudget(Prepid id, Rtable *source)
{
  Prep *p = find(id);
  int   error;
  if (!p || p->source != source)
    return -ERR_BAD_HANDLE;
  if (p->origin != PREP_EXEC)
    return -ERR_STATE;
  error = preppublishcheck(id);
  return error ? error : localexecbudget(source, p->work.exec.ticket);
}

int
prepsnapshotcheck(Prepid id, Rtable *source, Rtable *target)
{
  Prep *p = find(id);
  if (!p || p->source != source)
    return -ERR_BAD_HANDLE;
  if (!active(p))
    return p->expired ? -ERR_TIMEDOUT : -ERR_REVOKED;
  if (p->target != target)
    return -ERR_BAD_HANDLE;
  return p->origin == PREP_FORK && p->cloning == PREP_CLONING ? 0 : -ERR_STATE;
}
int
prepforkcheckpoint(Forkcheck checkpoint, Prepid id, Rtable *source, Rtable *target)
{
  int error = checkpoint(id);
  return error ? error : prepsnapshotcheck(id, source, target);
}
int
prepforktree(Prepid id, Spacetxn **out)
{
  Prep *p = find(id);
  if (!p || !out || p->origin != PREP_FORK || p->cloning != PREP_CLONING)
    return -ERR_STATE;
  *out = &p->work.tree;
  return 0;
}
int
prepforkopen(Prepid id, Spaceid source, Spaceid target, Spacetxn **out)
{
  Prep *p = find(id);
  if (!p || !out || source.reserved || target.reserved || p->origin != PREP_FORK ||
      p->cloning != PREP_CLONING || source.index != p->sourcespace.index ||
      source.generation != p->sourcespace.generation || target.index != p->space.index ||
      target.generation != p->space.generation || !active(p))
    return -ERR_STATE;
  *out = &p->work.tree;
  return 0;
}
int
prepforkfinish(Prepid id)
{
  Prep *p = find(id);
  if (!p || p->origin != PREP_FORK || p->cloning != PREP_CLONING || p->work.tree.phase)
    return -ERR_STATE;
  p->cloning = 0;
  if (p->state != PREP_ACTIVE)
    p->target = NULL;
  if (!active(p))
    return p->expired ? -ERR_TIMEDOUT : -ERR_REVOKED;
  return 0;
}
int
prepforkabandon(Prepid id)
{
  Prep *p = find(id);
  int   error;
  if (!p || p->origin != PREP_FORK || p->cloning != PREP_CLONING || !p->work.tree.phase ||
      !p->target || p->target->epoch != p->targetepoch || p->target->owner != p->pid ||
      p->target->image != p->image || p->target->state != LOCAL_OPEN || p->temporary)
    return -ERR_STATE;
  error = spacesnapshotabortcheck(p->sourcespace, p->space);
  if (error)
    return error == SP_BUSY ? -ERR_BUSY : -ERR_STATE;
  error = prephold(id);
  if (error)
    return error;
  error = prepcancel(id);
  if (error) {
    prepdrop(id);
    return error;
  }
  p->cloning = PREP_DRAIN;
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
  if (p->cloning)
    return -ERR_BUSY;
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
    if (p->state == PREP_ACTIVE || p->temporary || p->leased || p->cloning ||
        (p->origin == PREP_EXEC ? p->work.exec.ticket != 0 : p->work.tree.phase != 0))
      panic("live preparation without owner");
    if (imagedrop(p->identity, IMAGE_CLEANUP))
      panic("preparation image reference");
    p->state = 0;
  }
  return 0;
}
int
preppublishcheck(Prepid id)
{
  Prep *p = find(id);
  if (!p)
    return -ERR_REVOKED;
  if (!active(p))
    return p->expired ? -ERR_TIMEDOUT : -ERR_REVOKED;
  return p->temporary || p->cloning ? -ERR_BUSY : 0;
}
static void
releaseview(Prep *p)
{
  if (p->origin == PREP_EXEC) {
    if (acctexecend(p->acct, p->work.exec.ticket))
      panic("exec preparation ticket release");
    p->work.exec.ticket = 0;
  }
  if (p->leased && spacedrop(p->space))
    panic("preparation space lease");
  p->leased = 0;
  if (!p->cloning)
    p->target = NULL;
}
void
prepcomplete(Prepid id)
{
  Prep *p = find(id);
  Image image;
  if (!p || p->state != PREP_ACTIVE || p->temporary || p->cloning ||
      imagestat(p->identity, &image) || image.state != IMAGE_ACTIVE)
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
  if (p->origin != PREP_EXEC) {
    if (lifestat(p->life, &l))
      panic("preparation target life");
    if (l.state == LIFE_CONSTRUCT) {
      if (lifecancel(p->life))
        panic("preparation cancel");
    } else if (l.state != LIFE_ABORTED)
      return -ERR_STATE;
  }
  if (imageend(p->identity, IMAGE_ABORT))
    panic("preparation image abort");
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
  if (p->cloning)
    return -ERR_BUSY;
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
  if (p->cloning)
    return -ERR_BUSY;
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
  if (!slots || slots > 32 || p->state == PREP_ACTIVE || p->cloning)
    return -ERR_STATE;
  if (p->source && p->source->epoch == p->sourceepoch && spacefrozen(p->sourcespace) > 0)
    return 1;
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
    if (p->state && (p->cloning == PREP_DRAIN || p->cloning == PREP_ROLLBACK)) {
      int more;
      if (p->state != PREP_ABORTED || !p->target || p->target->epoch != p->targetepoch ||
          p->target->owner != p->pid || p->target->image != p->image)
        panic("fork cleanup owner identity");
      if (p->cloning == PREP_DRAIN) {
        if (localclose(p->target))
          panic("fork cleanup table close");
        more = localdrain(p->target, 32);
        if (more < 0)
          panic("fork cleanup table drain");
        if (!more)
          p->cloning = PREP_ROLLBACK;
      } else {
        more = spacesnapshotstep(p->sourcespace, p->space, 0);
        if (more < 0)
          panic("fork cleanup tree rollback");
        if (!more) {
          p->cloning = 0;
          p->target = NULL;
          if (prepdrop(id))
            panic("fork cleanup worker reference");
        }
      }
      worked++;
      continue;
    }
    if (!p->state || p->state == PREP_ACTIVE || p->cloning || !p->source ||
        p->cursor >= p->source->count)
      continue;
    if (prepclean(id, 32) < 0)
      panic("preparation CPU cleanup");
    worked++;
  }
  return (int) worked;
}

int
prepexpire(u64 now, u32 scans)
{
  static u32 cursor;
  u32        i, expired = 0;
  if (!scans || scans > 8)
    return -ERR_INVALID;
  for (i = 0; i < scans; i++) {
    Prep *p = &preps[cursor];
    cursor = (cursor + 1) % PREPS;
    if (p->state == PREP_ACTIVE && now >= p->deadline) {
      expire(p, now);
      expired++;
    }
  }
  return (int) expired;
}

int
preprelated(Handle owner, u64 sourceimage, Handle target, u64 number, Imageid *out)
{
  Prep *p;
  Image image;
  Life  life;
  u32   i;
  int   usable;
  if (!out || handletype(owner) != HANDLE_PROCESS || !sourceimage ||
      handletype(target) != HANDLE_PROCESS || (target == owner && !number))
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  for (i = 0; i < PREPS; i++) {
    p = &preps[i];
    if (!p->state || p->owner != owner || p->sourceimage != sourceimage || p->pid != target ||
        (number && p->image != number))
      continue;
    if (p->cloning)
      return -ERR_BUSY;
    usable = p->state == PREP_ACTIVE ? active(p) : 0;
    if (p->state == PREP_COMMITTED || (p->state == PREP_ACTIVE && !usable))
      return -ERR_STATE;
    if (imagestat(p->identity, &image) || lifestat(image.life, &life) || image.pid != target ||
        image.sourceimage != sourceimage || (target != owner && life.parent != owner))
      return -ERR_STALE;
    if ((p->state == PREP_ACTIVE && image.state != IMAGE_PREPARING) ||
        (p->state == PREP_ABORTED && image.state != IMAGE_ENDED))
      return -ERR_STATE;
    /* A canceled retained scope keeps the original owner identity even after
       its source table closes/rebinds. It cannot mint fresh business rights. */
    out->generation = p->identity.generation;
    out->index = p->identity.index;
    return 0;
  }
  return -ERR_STALE;
}
