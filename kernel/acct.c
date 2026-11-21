#include <acct.h>

/* Page.owner packs this internal generation and the 8-bit accounting slot. */
#define GENERATION_MAX 0x00ffffffffffffffUL
static Set  sets[AC_SETS];
static Acct accounts[AC_ACCTS];
static u64  exectickets[AC_ACCTS];
static u64  execcursor;

static Set *
setfind(Setid id)
{
  Set *s;
  if (id.reserved || id.index >= AC_SETS)
    return NULL;
  s = &sets[id.index];
  return s->state == AC_ACTIVE && s->generation == id.generation ? s : NULL;
}

static Acct *
find(Acctid id)
{
  Acct *a;
  if (id.reserved || id.index >= AC_ACCTS)
    return NULL;
  a = &accounts[id.index];
  return a->state != AC_FREE && a->generation == id.generation ? a : NULL;
}

static void
reap(Acct *a)
{
  if (a->state == AC_CLOSING && !a->bound && !a->refs && !a->used && !a->reserved) {
    sets[a->set].refs--;
    a->state = AC_FREE;
  }
}

int
setcreate(u64 limit, u64 peak, u32 max_instances, Setid *out)
{
  u32  i;
  Set *s;
  if (!out || !limit || peak < limit || !max_instances)
    return AC_BAD;
  for (i = 0; i < AC_SETS; i++) {
    s = &sets[i];
    if (s->state != AC_FREE || s->generation == GENERATION_MAX)
      continue;
    s->generation++;
    s->limit = limit;
    s->exec_limit = peak;
    s->charged = 0;
    s->instances = 0;
    s->max_instances = max_instances;
    s->refs = 1;
    s->leases = 0;
    s->state = AC_ACTIVE;
    out->generation = s->generation;
    out->index = i;
    out->reserved = 0;
    return 0;
  }
  return AC_FULL;
}

int
setclose(Setid id)
{
  Set *s = setfind(id);
  if (!s)
    return AC_BAD;
  if (s->refs != 1 || s->charged || s->instances)
    return AC_BUSY;
  s->refs = 0;
  s->state = AC_FREE;
  return 0;
}

int
setenter(Setid id)
{
  Set *s = setfind(id);
  if (!s)
    return AC_BAD;
  if (s->instances >= s->max_instances || s->refs == 0xffffffffU)
    return AC_LIMIT;
  s->instances++;
  s->refs++;
  return 0;
}

int
setleave(Setid id)
{
  Set *s = setfind(id);
  if (!s || !s->instances)
    return AC_BAD;
  s->instances--;
  s->refs--;
  return 0;
}

int
setstat(Setid id, Set *out)
{
  Set *s = setfind(id);
  if (!s || !out)
    return AC_BAD;
  out->generation = s->generation;
  out->limit = s->limit;
  out->exec_limit = s->exec_limit;
  out->charged = s->charged;
  out->instances = s->instances;
  out->max_instances = s->max_instances;
  out->refs = s->refs;
  out->leases = s->leases;
  out->state = s->state;
  return 0;
}

int
sethold(Setid id)
{
  Set *s = setfind(id);
  if (!s)
    return AC_BAD;
  if (s->refs == ~(u32) 0 || s->leases == ~(u32) 0)
    return AC_LIMIT;
  s->refs++;
  s->leases++;
  return 0;
}
int
setdrop(Setid id)
{
  Set *s = setfind(id);
  if (!s || !s->leases)
    return AC_BAD;
  s->refs--;
  s->leases--;
  return 0;
}
u64
settag(Setid id)
{
  return setfind(id) ? (id.generation << 8) | id.index : 0;
}
int
setlookup(u64 tag, Setid *out)
{
  Setid id;
  if (!out)
    return AC_BAD;
  out->generation = 0;
  out->index = out->reserved = 0;
  id.generation = tag >> 8;
  id.index = (u32) (tag & 255);
  id.reserved = 0;
  if (!id.generation || !setfind(id))
    return AC_BAD;
  out->generation = id.generation;
  out->index = id.index;
  return 0;
}
int
acctcreate(Setid id, u64 limit, u64 peak, Acctid *out)
{
  Set  *s = setfind(id);
  Acct *a;
  u32   i;
  if (!s || !out || !limit || peak < limit)
    return AC_BAD;
  if (s->refs == 0xffffffffU)
    return AC_LIMIT;
  for (i = 0; i < AC_ACCTS; i++) {
    a = &accounts[i];
    if (a->state != AC_FREE || a->generation == GENERATION_MAX)
      continue;
    a->generation++;
    a->limit = limit;
    a->exec_limit = peak;
    a->used = a->reserved = a->cow = 0;
    a->set = id.index;
    a->state = AC_ACTIVE;
    a->refs = a->bound = 1;
    s->refs++;
    out->generation = a->generation;
    out->index = i;
    out->reserved = 0;
    return 0;
  }
  return AC_FULL;
}

int
accthold(Acctid id)
{
  Acct *a = find(id);
  if (!a)
    return AC_BAD;
  if (a->refs == 0xffffffffU)
    return AC_LIMIT;
  a->refs++;
  return 0;
}

int
acctdrop(Acctid id)
{
  Acct *a = find(id);
  if (!a || a->refs <= a->bound)
    return AC_BAD;
  a->refs--;
  reap(a);
  return 0;
}

int
acctclose(Acctid id)
{
  Acct *a = find(id);
  if (!a || a->state != AC_ACTIVE)
    return AC_BAD;
  a->state = AC_CLOSING;
  exectickets[id.index] = 0;
  return 0;
}

int
acctunbind(Acctid id)
{
  Acct *a = find(id);
  if (!a || a->state != AC_CLOSING || !a->bound || !a->refs)
    return AC_BAD;
  a->bound = 0;
  a->refs--;
  reap(a);
  return 0;
}

int
acctstat(Acctid id, Acct *out)
{
  Acct *a = find(id);
  if (!a || !out)
    return AC_BAD;
  out->generation = a->generation;
  out->limit = a->limit;
  out->exec_limit = a->exec_limit;
  out->used = a->used;
  out->reserved = a->reserved;
  out->cow = a->cow;
  out->set = a->set;
  out->state = a->state;
  out->refs = a->refs;
  out->bound = a->bound;
  return 0;
}

u64
accttag(Acctid id)
{
  return find(id) ? (id.generation << 8) | id.index : 0;
}
int
acctlookup(u64 tag, Acctid *out)
{
  Acctid id;
  if (!out)
    return AC_BAD;
  out->generation = 0;
  out->index = out->reserved = 0;
  id.generation = tag >> 8;
  id.index = (u32) (tag & 255);
  id.reserved = 0;
  if (!id.generation || !find(id))
    return AC_BAD;
  out->generation = id.generation;
  out->index = id.index;
  return 0;
}
int
acctbudget(Acctid id, u64 limit, u64 peak)
{
  Acct *a = find(id);
  if (!a || a->state != AC_ACTIVE || peak < limit)
    return AC_BAD;
  if (a->used > limit || a->reserved > limit - a->used)
    return AC_BUSY;
  a->limit = limit;
  a->exec_limit = peak;
  return 0;
}

static int
reserve(Acctid id, u64 pages, u64 ticket)
{
  Acct *a = find(id);
  Set  *s;
  u64   total, limit, setlimit;
  if (!a || a->state != AC_ACTIVE || !pages || (ticket && exectickets[id.index] != ticket))
    return AC_BAD;
  s = &sets[a->set];
  total = a->used + a->reserved;
  limit = ticket ? a->exec_limit : a->limit;
  setlimit = ticket ? s->exec_limit : s->limit;
  if (total > limit || pages > limit - total || s->charged > setlimit ||
      pages > setlimit - s->charged)
    return AC_LIMIT;
  a->reserved += pages;
  s->charged += pages;
  return 0;
}

int
acctreserve(Acctid id, u64 pages)
{
  return reserve(id, pages, 0);
}

int
acctconvert(Acctid id, u64 pages)
{
  Acct *a = find(id);
  if (!a || !pages || pages > a->reserved - a->cow)
    return AC_BAD;
  a->reserved -= pages;
  a->used += pages;
  return 0;
}

int
acctrelease(Acctid id, u64 used, u64 reserved)
{
  Acct *a = find(id);
  if (!a || (!used && !reserved) || used > a->used || reserved > a->reserved - a->cow)
    return AC_BAD;
  a->used -= used;
  a->reserved -= reserved;
  sets[a->set].charged -= used + reserved;
  reap(a);
  return 0;
}

static int
alloc(Acctid id, u64 ticket, u32 order, const Pglim *lim, Acctblk *out)
{
  Pgblk block;
  u64   pages, owner;
  int   error;
  if (!out || order > PG_ORDER)
    return AC_BAD;
  pages = 1UL << order;
  error = accthold(id);
  if (error)
    return error;
  error = reserve(id, pages, ticket);
  if (error) {
    acctdrop(id);
    return error;
  }
  error = pgalloc(order, lim, &block);
  if (error) {
    acctrelease(id, 0, pages);
    acctdrop(id);
    return error == PG_NOMEM ? AC_NOMEM : AC_BAD;
  }
  owner = (id.generation << 8) | id.index;
  if (pgclaim(block, owner)) {
    pgfree(block);
    acctrelease(id, 0, pages);
    acctdrop(id);
    return AC_BAD;
  }
  acctconvert(id, pages);
  out->block.pa = block.pa;
  out->block.order = block.order;
  out->block.reserved = 0;
  out->acct.generation = id.generation;
  out->acct.index = id.index;
  out->acct.reserved = 0;
  return 0;
}

int
acctalloc(Acctid id, u32 order, const Pglim *lim, Acctblk *out)
{
  return alloc(id, 0, order, lim, out);
}

int
acctexecbegin(Acctid id, u64 *ticket)
{
  Acct *a = find(id);
  if (!ticket || !a || a->state != AC_ACTIVE)
    return AC_BAD;
  if (exectickets[id.index])
    return AC_BUSY;
  if (execcursor == ~(u64) 0)
    return AC_FULL;
  exectickets[id.index] = ++execcursor;
  *ticket = execcursor;
  return 0;
}

int
acctexecalloc(Acctid id, u64 ticket, u32 order, const Pglim *lim, Acctblk *out)
{
  return ticket ? alloc(id, ticket, order, lim, out) : AC_BAD;
}

int
acctexecend(Acctid id, u64 ticket)
{
  Acct *a = find(id);
  if (!a || a->state != AC_ACTIVE || !ticket || exectickets[id.index] != ticket)
    return AC_BAD;
  exectickets[id.index] = 0;
  return 0;
}

int
acctfree(Acctblk block)
{
  Acct *a = find(block.acct);
  u64   pages, owner;
  if (!a || block.block.order > PG_ORDER)
    return AC_BAD;
  pages = 1UL << block.block.order;
  if (a->used < pages || a->refs <= a->bound)
    return AC_BAD;
  owner = (block.acct.generation << 8) | block.acct.index;
  if (pgrelease(block.block, owner))
    return AC_BAD;
  acctrelease(block.acct, pages, 0);
  acctdrop(block.acct);
  return 0;
}

static Acct *
cowaccount(const Acctcow *credit)
{
  Acct *a;
  if (!credit || !credit->physical.pages)
    return NULL;
  a = find(credit->acct);
  return a && credit->physical.pages <= a->cow && a->cow <= a->reserved &&
                 credit->physical.pages <= pgreserved() && a->refs > a->bound
             ? a
             : NULL;
}
static void
cowfinish(Acctcow *credit)
{
  if (!credit->physical.pages) {
    acctdrop(credit->acct);
    credit->acct.generation = 0;
    credit->acct.index = credit->acct.reserved = 0;
  }
}
int
acctcowreserve(Acctid id, u64 pages, Acctcow *out)
{
  Acct    *a = find(id);
  Pgcredit physical;
  int      error;
  if (!a || !out || !pages || out->physical.pages || out->acct.generation || out->acct.index ||
      out->acct.reserved)
    return AC_BAD;
  error = accthold(id);
  if (error)
    return error;
  error = acctreserve(id, pages);
  if (error) {
    acctdrop(id);
    return error;
  }
  physical.pages = 0;
  error = pgreserve(&physical, pages);
  if (error) {
    acctrelease(id, 0, pages);
    acctdrop(id);
    return error == PG_NOMEM ? AC_NOMEM : AC_BAD;
  }
  a->cow += pages;
  out->acct.generation = id.generation;
  out->acct.index = id.index;
  out->acct.reserved = 0;
  out->physical.pages = physical.pages;
  return 0;
}
int
acctcowcancel(Acctcow *credit, u64 pages)
{
  Acct *a = cowaccount(credit);
  if (!a || !pages || pages > credit->physical.pages)
    return AC_BAD;
  if (pgcancel(&credit->physical, pages))
    return AC_BAD;
  a->cow -= pages;
  a->reserved -= pages;
  sets[a->set].charged -= pages;
  cowfinish(credit);
  return 0;
}
int
acctcowalloc(Acctcow *credit, Acctblk *out)
{
  Acct  *a = cowaccount(credit);
  Pgblk  block;
  Acctid id;
  int    error;
  if (!a || !out)
    return AC_BAD;
  id = credit->acct;
  error = accthold(id);
  if (error)
    return error;
  error = pgredeem(&credit->physical, &block);
  if (error) {
    acctdrop(id);
    return error == PG_NOMEM ? AC_NOMEM : AC_BAD;
  }
  if (pgclaim(block, (id.generation << 8) | id.index)) {
    /* Serialized rollback returns the just-redeemed capacity before exposing
       a page or changing the account. This cannot compete with admission. */
    pgfree(block);
    pgreserve(&credit->physical, 1);
    acctdrop(id);
    return AC_BAD;
  }
  a->cow--;
  a->reserved--;
  a->used++;
  out->block.pa = block.pa;
  out->block.order = out->block.reserved = 0;
  out->acct.generation = id.generation;
  out->acct.index = id.index;
  out->acct.reserved = 0;
  cowfinish(credit);
  return 0;
}
int
acctcowmove(Acctblk *block, Acctcow *credit)
{
  Acct  *to = cowaccount(credit), *from;
  Acctid target, source;
  int    error;
  if (!to || !block || block->block.order || block->block.reserved)
    return AC_BAD;
  from = find(block->acct);
  if (!from || from == to || from->set != to->set || !from->used || from->refs <= from->bound)
    return AC_BAD;
  source = block->acct;
  target = credit->acct;
  error = accthold(target);
  if (error)
    return error;
  /* Validate and change physical ownership before the infallible quota
     commit. Invalid/stale block descriptions never consume a reservation. */
  error = pgtransfer(block->block, (source.generation << 8) | source.index,
                     (target.generation << 8) | target.index);
  if (error) {
    acctdrop(target);
    return AC_BAD;
  }
  if (pgcancel(&credit->physical, 1)) {
    pgtransfer(block->block, (target.generation << 8) | target.index,
               (source.generation << 8) | source.index);
    acctdrop(target);
    return AC_BAD;
  }
  from->used--;
  to->cow--;
  to->reserved--;
  to->used++;
  sets[from->set].charged--;
  block->acct.generation = target.generation;
  block->acct.index = target.index;
  block->acct.reserved = 0;
  acctdrop(source);
  cowfinish(credit);
  return 0;
}

int
acctcowreplace(Acctblk *old, Acctcow *credit, Acctblk *out)
{
  Acct  *to = cowaccount(credit), *from;
  Acctid source, target;
  Pgblk  fresh;
  u64    owner;
  int    error;
  if (!to || !old || !out || old == out || old->block.order || old->block.reserved)
    return AC_BAD;
  from = find(old->acct);
  if (!from || from == to || from->set != to->set || !from->used || from->refs <= from->bound)
    return AC_BAD;
  source = old->acct;
  target = credit->acct;
  owner = (source.generation << 8) | source.index;
  if (pgowned(old->block, owner))
    return AC_BAD;
  /* The original block's payer reference is retained for fresh; this added
     peer reference will belong to old. No new payer charge or hold is needed. */
  error = accthold(target);
  if (error)
    return error;
  error = pgredeem(&credit->physical, &fresh);
  if (error) {
    acctdrop(target);
    return error == PG_NOMEM ? AC_NOMEM : AC_BAD;
  }
  if (pgclaim(fresh, owner)) {
    pgfree(fresh);
    pgreserve(&credit->physical, 1);
    acctdrop(target);
    return AC_BAD;
  }
  if (pgtransfer(old->block, owner, (target.generation << 8) | target.index)) {
    pgrelease(fresh, owner);
    pgreserve(&credit->physical, 1);
    acctdrop(target);
    return AC_BAD;
  }
  /* Both physical ownership changes succeeded; the rest cannot fail under
     the caller's serialization. The payer remains at its original used sum. */
  to->cow--;
  to->reserved--;
  to->used++;
  out->block.pa = fresh.pa;
  out->block.order = out->block.reserved = 0;
  out->acct.generation = source.generation;
  out->acct.index = source.index;
  out->acct.reserved = 0;
  old->acct.generation = target.generation;
  old->acct.index = target.index;
  old->acct.reserved = 0;
  cowfinish(credit);
  return 0;
}

int
acctcheck(void)
{
  volatile u64 charged[AC_SETS];
  volatile u32 refs[AC_SETS];
  u32          i;
  const Acct  *a;
  const Set   *s;
  for (i = 0; i < AC_SETS; i++) {
    charged[i] = 0;
    refs[i] = 0;
  }
  for (i = 0; i < AC_ACCTS; i++) {
    a = &accounts[i];
    if (exectickets[i] && (a->state != AC_ACTIVE || exectickets[i] > execcursor))
      return AC_BAD;
    if (a->state == AC_FREE)
      continue;
    if (a->state > AC_CLOSING || a->set >= AC_SETS || !a->generation ||
        a->generation > GENERATION_MAX || a->bound > 1 || a->refs < a->bound ||
        a->cow > a->reserved || a->used > ~(u64) 0 - a->reserved ||
        a->used + a->reserved > a->exec_limit || a->exec_limit < a->limit)
      return AC_BAD;
    if (charged[a->set] > ~(u64) 0 - a->used - a->reserved)
      return AC_BAD;
    charged[a->set] += a->used + a->reserved;
    refs[a->set]++;
  }
  for (i = 0; i < AC_SETS; i++) {
    s = &sets[i];
    if (s->state == AC_FREE) {
      if (charged[i] || refs[i])
        return AC_BAD;
    } else if (s->state != AC_ACTIVE || s->charged != charged[i] || s->charged > s->exec_limit ||
               s->exec_limit < s->limit || s->instances > s->max_instances ||
               (u64) s->refs != 1UL + refs[i] + s->instances + s->leases)
      return AC_BAD;
  }
  return 0;
}
