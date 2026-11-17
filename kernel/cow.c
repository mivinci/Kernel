#include <cow.h>
#include <vm.h>
#define NONE           0xffffffffU
#define GROUP_RESERVED 1
#define GROUP_ACTIVE   2
typedef struct Cowgroup Cowgroup;
typedef struct Cowref   Cowref;
struct Cowgroup
{
  Acctblk backing;
  u32     head, payer, count, state;
};
struct Cowref
{
  u64     generation;
  Acctcow credit;
  u32     group, home, previous, next, state;
};
STATIC_ASSERT(sizeof(Cowgroup) == 48, cow_group_size);
STATIC_ASSERT(sizeof(Cowref) == 56, cow_ref_size);
static Cowgroup groups[COW_REFS];
static Cowref   refs[COW_REFS];
static u32      initialized, freegroup, freeref, retired;
void
cowinit(void)
{
  u32 i;
  if (initialized)
    return;
  for (i = 0; i < COW_REFS; i++) {
    groups[i].head = refs[i].next = i + 1 == COW_REFS ? NONE : i + 1;
  }
  freegroup = freeref = 0;
  initialized = 1;
}
static Cowref *
find(Cowid id)
{
  Cowref *r;
  if (!initialized || id.reserved || id.index >= COW_REFS)
    return NULL;
  r = &refs[id.index];
  return r->state && r->generation == id.generation ? r : NULL;
}
static int
reserve(u32 *index)
{
  Cowref   *r;
  Cowgroup *g;
  u32       slot, home;
  if (freeref == NONE || freegroup == NONE)
    return AC_FULL;
  slot = freeref;
  home = freegroup;
  r = &refs[slot];
  g = &groups[home];
  freeref = r->next;
  freegroup = g->head;
  r->generation++;
  r->state = 1;
  r->home = home;
  r->group = r->previous = r->next = NONE;
  r->credit.acct.generation = r->credit.acct.index = r->credit.acct.reserved = 0;
  r->credit.physical.pages = 0;
  g->state = GROUP_RESERVED;
  g->payer = slot;
  g->head = NONE;
  g->count = 0;
  *index = slot;
  return 0;
}
static void
freehome(u32 home)
{
  groups[home].state = groups[home].count = 0;
  groups[home].head = freegroup;
  freegroup = home;
}
static void
freereference(u32 index)
{
  Cowref *r = &refs[index];
  r->state = 0;
  if (r->generation == COW_GENERATION_MAX) {
    retired++;
  } else {
    r->next = freeref;
    freeref = index;
  }
}
static void
copyblock(Acctblk *to, const Acctblk *from)
{
  to->block.pa = from->block.pa;
  to->block.order = from->block.order;
  to->block.reserved = from->block.reserved;
  to->acct.generation = from->acct.generation;
  to->acct.index = from->acct.index;
  to->acct.reserved = from->acct.reserved;
}
static void
publish(u32 index, Cowid *out)
{
  out->generation = refs[index].generation;
  out->index = index;
  out->reserved = 0;
}
int
cowcreate(Acctblk *owned, Cowid *out)
{
  Acct      account;
  Cowgroup *g;
  Cowref   *r;
  u32       index;
  int       error;
  if (!initialized || !owned || !out || owned->block.order || owned->block.reserved ||
      acctstat(owned->acct, &account) || !account.used || account.refs <= account.bound ||
      pgowned(owned->block, accttag(owned->acct)))
    return AC_BAD;
  error = reserve(&index);
  if (error)
    return error;
  r = &refs[index];
  r->group = r->home;
  g = &groups[r->group];
  copyblock(&g->backing, owned);
  g->head = g->payer = index;
  g->count = 1;
  g->state = GROUP_ACTIVE;
  publish(index, out);
  owned->block.pa = owned->block.order = owned->block.reserved = 0;
  owned->acct.generation = owned->acct.index = owned->acct.reserved = 0;
  return 0;
}
static Acctid
accountof(u32 index)
{
  Cowref   *r = &refs[index];
  Cowgroup *g = &groups[r->group];
  return g->payer == index ? g->backing.acct : r->credit.acct;
}
int
cowshare(Cowid source, Acctid target, Cowid *out)
{
  Cowref   *from = find(source), *r;
  Cowgroup *g;
  Acctid    owner;
  Acct      a, b;
  u32       index, at;
  int       error;
  if (!from || !out)
    return AC_BAD;
  owner = accountof(source.index);
  if (acctstat(owner, &a) || acctstat(target, &b) || a.state != AC_ACTIVE || b.state != AC_ACTIVE ||
      a.set != b.set || (owner.generation == target.generation && owner.index == target.index))
    return AC_BAD;
  for (at = groups[from->group].head; at != NONE; at = refs[at].next) {
    owner = accountof(at);
    if (owner.generation == target.generation && owner.index == target.index)
      return AC_BAD;
  }
  error = reserve(&index);
  if (error)
    return error;
  r = &refs[index];
  error = acctcowreserve(target, 1, &r->credit);
  if (error) {
    freehome(r->home);
    freereference(index);
    return error;
  }
  g = &groups[from->group];
  r->group = from->group;
  r->next = g->head;
  refs[g->head].previous = index;
  g->head = index;
  g->count++;
  publish(index, out);
  return 0;
}
static void
unlink(u32 index)
{
  Cowref   *r = &refs[index];
  Cowgroup *g = &groups[r->group];
  if (r->previous == NONE)
    g->head = r->next;
  else
    refs[r->previous].next = r->next;
  if (r->next != NONE)
    refs[r->next].previous = r->previous;
  g->count--;
  r->previous = r->next = NONE;
}
static u32
peerof(u32 index)
{
  Cowgroup *g = &groups[refs[index].group];
  return g->head == index ? refs[index].next : g->head;
}
int
cowwrite(Cowid id, u64 *pa)
{
  Cowref             *r = find(id), *peer;
  Cowgroup           *g, *freshgroup;
  Acctblk             fresh;
  volatile u64       *to;
  const volatile u64 *from;
  u64                 oldpa;
  u32                 peerindex, i, home;
  int                 error;
  if (!r || !pa)
    return AC_BAD;
  g = &groups[r->group];
  if (g->count == 1) {
    *pa = g->backing.block.pa;
    return 0;
  }
  oldpa = g->backing.block.pa;
  if (g->payer == id.index) {
    peerindex = peerof(id.index);
    peer = &refs[peerindex];
    error = acctcowreplace(&g->backing, &peer->credit, &fresh);
    if (error)
      return error;
    home = peer->home;
    peer->home = r->home;
    r->home = home;
    g->payer = peerindex;
  } else {
    error = acctcowalloc(&r->credit, &fresh);
    if (error)
      return error;
  }
  to = kaddr(fresh.block.pa);
  from = kaddr(oldpa);
  for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
    to[i] = from[i];
  unlink(id.index);
  r->group = r->home;
  freshgroup = &groups[r->home];
  copyblock(&freshgroup->backing, &fresh);
  freshgroup->state = GROUP_ACTIVE;
  freshgroup->head = freshgroup->payer = id.index;
  freshgroup->count = 1;
  *pa = fresh.block.pa;
  return 0;
}
int
cowdrop(Cowid id)
{
  Cowref   *r = find(id), *peer;
  Cowgroup *g;
  u32       peerindex;
  int       error;
  if (!r)
    return AC_BAD;
  g = &groups[r->group];
  if (g->count == 1) {
    error = acctfree(g->backing);
    if (error)
      return error;
    freehome(r->home);
  } else if (g->payer == id.index) {
    peerindex = peerof(id.index);
    peer = &refs[peerindex];
    error = acctcowmove(&g->backing, &peer->credit);
    if (error)
      return error;
    freehome(peer->home);
    peer->home = r->home;
    g->payer = peerindex;
    unlink(id.index);
  } else {
    error = acctcowcancel(&r->credit, 1);
    if (error)
      return error;
    freehome(r->home);
    unlink(id.index);
  }
  freereference(id.index);
  return 0;
}
int
cowstat(Cowid id, Cowstat *out)
{
  Cowref   *r = find(id);
  Cowgroup *g;
  Acctid    acct;
  if (!r || !out)
    return AC_BAD;
  g = &groups[r->group];
  acct = accountof(id.index);
  out->acct.generation = acct.generation;
  out->acct.index = acct.index;
  out->acct.reserved = 0;
  out->pa = g->backing.block.pa;
  out->branches = g->count;
  out->payer = g->payer == id.index;
  return 0;
}
void
cowpool(Cowpool *out)
{
  u32 i;
  out->branches = out->groups = out->prepaid = 0;
  out->retired = retired;
  for (i = 0; i < COW_REFS; i++) {
    out->branches += refs[i].state != 0;
    out->groups += groups[i].state == GROUP_ACTIVE;
    out->prepaid += groups[i].state == GROUP_RESERVED;
  }
}
int
cowcheck(void)
{
  u32       i, at, previous, count, seen = 0, live = 0, dead = 0;
  Cowgroup *g;
  Cowref   *r;
  Acct      account;
  if (!initialized)
    return AC_BAD;
  for (i = 0; i < COW_REFS; i++) {
    g = &groups[i];
    if (g->state == GROUP_ACTIVE) {
      if (!g->count || g->head >= COW_REFS || g->payer >= COW_REFS ||
          pgowned(g->backing.block, accttag(g->backing.acct)))
        return AC_BAD;
      previous = NONE;
      count = 0;
      for (at = g->head; at != NONE; at = r->next) {
        if (at >= COW_REFS || ++count > COW_REFS)
          return AC_BAD;
        r = &refs[at];
        if (!r->state || r->group != i || r->previous != previous)
          return AC_BAD;
        previous = at;
      }
      if (count != g->count || !refs[g->payer].state || refs[g->payer].group != i)
        return AC_BAD;
      seen += count;
    } else if (g->state == GROUP_RESERVED) {
      if (g->payer >= COW_REFS || !refs[g->payer].state || refs[g->payer].home != i)
        return AC_BAD;
    } else if (g->state)
      return AC_BAD;
    r = &refs[i];
    if (!r->state) {
      if (r->generation == COW_GENERATION_MAX)
        dead++;
      continue;
    }
    live++;
    if (!r->generation || r->group >= COW_REFS || r->home >= COW_REFS)
      return AC_BAD;
    g = &groups[r->group];
    if (g->state != GROUP_ACTIVE)
      return AC_BAD;
    if (g->payer == i) {
      if (r->home != r->group || r->credit.physical.pages || r->credit.acct.generation)
        return AC_BAD;
    } else if (r->credit.physical.pages != 1 || groups[r->home].state != GROUP_RESERVED ||
               groups[r->home].payer != i || acctstat(r->credit.acct, &account) || !account.cow)
      return AC_BAD;
  }
  if (seen != live || dead != retired)
    return AC_BAD;
  count = 0;
  for (at = freeref; at != NONE; at = refs[at].next)
    if (at >= COW_REFS || ++count > COW_REFS || refs[at].state ||
        refs[at].generation == COW_GENERATION_MAX)
      return AC_BAD;
  if (count + live + retired != COW_REFS)
    return AC_BAD;
  count = 0;
  for (at = freegroup; at != NONE; at = groups[at].head)
    if (at >= COW_REFS || ++count > COW_REFS || groups[at].state)
      return AC_BAD;
  return count + live == COW_REFS ? 0 : AC_BAD;
}
