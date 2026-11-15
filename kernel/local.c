#include <local.h>
#include <diag.h>
#include <prep.h>

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
static Local *
find(Rtable *t, Handle h, u32 subtype)
{
  Local *l;
  u32    slot;
  if (!t || t->state != LOCAL_OPEN || handletype(h) != HANDLE_RESOURCE)
    return NULL;
  slot = handleindex(h);
  if (slot >= t->count)
    return NULL;
  l = &t->slots[slot];
  return l->subtype && l->generation == handlegen(h) && (!subtype || l->subtype == subtype) ? l
                                                                                            : NULL;
}
int
localnative(Rtable *t, Handle h)
{
  Local *l = find(t, h, 0);
  if (!l)
    return -ERR_BAD_HANDLE;
  return l->subtype == LOCAL_CONSTRUCT || (l->subtype == LOCAL_MEM && l->source == LOCAL_LOAD)
             ? -ERR_NOTSUP
             : 0;
}
static Local *
empty(Rtable *t)
{
  u32 i;
  if (!t || t->state != LOCAL_OPEN)
    return NULL;
  for (i = 0; i < t->count; i++)
    if (!t->slots[i].subtype && t->slots[i].generation < HANDLE_GENERATION_MAX)
      return &t->slots[i];
  return NULL;
}
static int
sloterror(Rtable *t)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (!t->slots[i].subtype && t->slots[i].generation == HANDLE_GENERATION_MAX)
      return -ERR_OVERFLOW;
  return -ERR_CAPACITY;
}
int
localroom(Rtable *t)
{
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  return empty(t) ? 0 : sloterror(t);
}
static Memid
memory(const Local *l)
{
  Memid id;
  id.generation = l->objectgen;
  id.index = l->object;
  id.reserved = 0;
  return id;
}
static int
window(const Local *l, u64 offset, u64 length)
{
  return length && offset >= l->offset && offset - l->offset <= l->length &&
         length <= l->length - (offset - l->offset);
}
static Prepid
loadscope(const Local *l)
{
  Prepid id;
  id.generation = l->offset;
  id.index = (u32) l->length;
  id.reserved = 0;
  return id;
}
static int
allowed(Rtable *t, const Local *l, u64 offset, u64 bytes)
{
  Mem m;
  int error;
  if (l->source != LOCAL_LOAD)
    return window(l, offset, bytes) ? 0 : -ERR_DENIED;
  error = prepcheck(loadscope(l), t, NULL);
  if (error)
    return error;
  if (memstat(memory(l), &m))
    return -ERR_STALE;
  return bytes && offset <= m.bytes && bytes <= m.bytes - offset ? 0 : -ERR_DENIED;
}
static void
publish(Rtable *t, Local *l, u32 subtype, u64 generation, u32 index, u64 rights, u64 offset,
        u64 length, u32 inherit, u32 source, Handle *out)
{
  l->generation++;
  l->objectgen = generation;
  l->object = index;
  l->rights = rights;
  l->offset = offset;
  l->length = length;
  l->inherit = (u8) inherit;
  l->source = (u8) source;
  l->subtype = (u16) subtype;
  *out = handlepack(HANDLE_RESOURCE, l->generation, (u32) (l - t->slots));
}
int
localinit(Rtable *t, Local *storage, u32 count, Acctid acct, Spaceid space)
{
  Acct  a;
  Space s;
  u32   i;
  if (!t || !storage || !count || count > LOCALS || (t->state && t->state != LOCAL_DEAD) ||
      acctstat(acct, &a) || a.state != AC_ACTIVE || spacestat(space, &s) ||
      s.acct.index != acct.index || s.acct.generation != acct.generation)
    return -ERR_INVALID;
  for (i = 0; i < count; i++)
    if (storage[i].subtype || storage[i].generation > HANDLE_GENERATION_MAX)
      return -ERR_STATE;
  if (t->epoch == ~(u64) 0)
    return -ERR_OVERFLOW;
  t->epoch++;
  t->owner = 0;
  t->image = 0;
  t->slots = storage;
  t->acct = acct;
  t->space = space;
  t->count = count;
  t->cursor = 0;
  t->state = LOCAL_OPEN;
  return 0;
}
int
localmem(Rtable *t, Memid id, u64 rights, u64 offset, u64 length, u32 inherit, Handle *out)
{
  Local *l;
  Mem    m;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!rights || rights & ~(LOCAL_GRANT | 15UL) || inherit & ~LOCAL_FORK ||
      (offset | length) & PAGE_MASK || !length || memstat(id, &m) || !m.managers ||
      offset > m.bytes || length > m.bytes - offset)
    return -ERR_INVALID;
  if (m.acct.index != t->acct.index || m.acct.generation != t->acct.generation)
    return -ERR_DENIED;
  if (m.state == MEM_RETIRING || (m.state == MEM_DATA && rights & (VM_EXEC | LOCAL_SEAL)) ||
      (m.state == MEM_SEALED && rights & VM_WRITE))
    return -ERR_DENIED;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = memhold(id);
  if (error)
    return convert(error);
  publish(t, l, LOCAL_MEM, id.generation, id.index, rights, offset, length, inherit, 1, out);
  return 0;
}
int
localconstruct(Rtable *t, Prepid id, u64 rights, Handle *out)
{
  Local *l;
  u32    i;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!rights || rights & ~31UL)
    return -ERR_INVALID;
  error = prepcheck(id, t, NULL);
  if (error)
    return error;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == LOCAL_CONSTRUCT && t->slots[i].objectgen == id.generation &&
        t->slots[i].object == id.index)
      return -ERR_STATE;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = prephold(id);
  if (error)
    return error;
  publish(t, l, LOCAL_CONSTRUCT, id.generation, id.index, rights, 0, 0, 0, 1, out);
  return 0;
}
int
localprepared(Rtable *t, Prepid prep, Memid id, u64 rights, Handle *out)
{
  Prepinfo info;
  Mem      m;
  Local   *l;
  int      error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = prepcheck(prep, t, &info);
  if (error)
    return error;
  if (!rights || rights & ~(LOCAL_GRANT | 15UL) || memstat(id, &m) || !m.managers ||
      m.acct.index != info.acct.index || m.acct.generation != info.acct.generation)
    return -ERR_INVALID;
  if ((m.state == MEM_DATA && rights & (VM_EXEC | LOCAL_SEAL)) ||
      (m.state == MEM_SEALED && rights & VM_WRITE))
    return -ERR_DENIED;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = memhold(id);
  if (error)
    return convert(error);
  error = prephold(prep);
  if (error) {
    memfree(id);
    return error;
  }
  publish(t, l, LOCAL_MEM, id.generation, id.index, rights, prep.generation, prep.index, 0,
          LOCAL_LOAD, out);
  return 0;
}
int
localtarget(Rtable *t, Handle h, u64 rights, Prepid *id, Handle *pid)
{
  Local   *l = find(t, h, LOCAL_CONSTRUCT);
  Prepinfo info;
  int      error;
  if (!l)
    return -ERR_BAD_HANDLE;
  if ((l->rights & rights) != rights)
    return -ERR_DENIED;
  id->generation = l->objectgen;
  id->index = l->object;
  id->reserved = 0;
  error = prepcheck(*id, t, &info);
  if (error)
    return -ERR_BAD_HANDLE;
  *pid = info.pid;
  return 0;
}
int
localspace(Rtable *t, u64 rights, Handle *out)
{
  Local *l;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!rights || rights & ~3UL)
    return -ERR_INVALID;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = spacehold(t->space);
  if (error)
    return convert(error);
  publish(t, l, LOCAL_SPACE, t->space.generation, t->space.index, rights, 0, 0, 0, 1, out);
  return 0;
}
int
localallocator(Rtable *t, u64 rights, Handle *out)
{
  Local *l;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!rights || rights & ~15UL)
    return -ERR_INVALID;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = accthold(t->acct);
  if (error)
    return convert(error);
  publish(t, l, LOCAL_ALLOC, t->acct.generation, t->acct.index, rights, 0, 0, 0, 1, out);
  return 0;
}
int
localcopy(Rtable *source, Handle h, Rtable *target, u64 rights, u64 offset, u64 length, u32 inherit,
          Handle *out)
{
  Local   *l = find(source, h, LOCAL_MEM);
  Prepinfo info;
  int      error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & LOCAL_GRANT) || rights & ~l->rights)
    return -ERR_DENIED;
  error = allowed(source, l, offset, length);
  if (error)
    return error;
  if (l->source == LOCAL_LOAD) {
    error = prepcheck(loadscope(l), source, &info);
    if (error)
      return error;
    if (target != info.target)
      return -ERR_DENIED;
    if (inherit & ~LOCAL_FORK)
      return -ERR_INVALID;
  } else if (inherit & ~l->inherit)
    return -ERR_DENIED;
  error = localmem(target, memory(l), rights, offset, length, inherit, out);
  if (!error)
    target->slots[handleindex(*out)].source = 3;
  return error;
}
int
localalloc(Rtable *t, Handle h, const Allocarg *a, Handle *out)
{
  Local   *l = find(t, h, 0);
  Prepid   prep;
  Prepinfo info;
  Acctid   payer;
  Handle   target;
  Memid    id;
  Acct     account;
  int      error;
  u64      rights;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!l || (l->subtype != LOCAL_ALLOC && l->subtype != LOCAL_CONSTRUCT))
    return -ERR_BAD_HANDLE;
  prep.generation = 0;
  prep.index = prep.reserved = 0;
  payer = t->acct;
  if (l->subtype == LOCAL_CONSTRUCT) {
    error = localtarget(t, h, PREP_ALLOC, &prep, &target);
    if (error)
      return error;
    error = prepcheck(prep, t, &info);
    if (error)
      return error;
    payer = info.acct;
  }
  if (!a || !a->bytes || a->kind < ALLOC_DATA || a->kind > ALLOC_DMA || a->reserved ||
      (a->kind != ALLOC_DMA && (a->align || a->minaddr || a->maxaddr)))
    return -ERR_INVALID;
  if (l->subtype == LOCAL_ALLOC && !(l->rights & (1UL << (a->kind - 1))))
    return -ERR_DENIED;
  if (a->kind != ALLOC_DATA && a->kind != ALLOC_CODE)
    return -ERR_NOTSUP;
  if (a->bytes > ~(u64) 0 - PAGE_MASK)
    return -ERR_OVERFLOW;
  if (acctstat(payer, &account) || account.state != AC_ACTIVE)
    return -ERR_STATE;
  if (a->bytes > (u64) MEM_PAGES * PAGE_SIZE)
    return -ERR_NOTSUP;
  if (!empty(t))
    return sloterror(t);
  error = a->kind == ALLOC_CODE ? memcode(payer, a->bytes, &id) : memcreate(payer, a->bytes, &id);
  if (error)
    return convert(error);
  rights = VM_READ | VM_WRITE | LOCAL_GRANT;
  if (a->kind == ALLOC_CODE)
    rights |= VM_EXEC | LOCAL_SEAL;
  error = prep.generation
              ? localprepared(t, prep, id, rights, out)
              : localmem(t, id, rights, 0, ALIGN_UP(a->bytes, PAGE_SIZE), LOCAL_FORK, out);
  if (memfree(id))
    panic("local allocation reference");
  if (!error && !prep.generation)
    t->slots[handleindex(*out)].source = 2;
  return error;
}
int
localmap(Rtable *t, Handle space, Handle mem, u64 offset, u64 address, u64 length, u64 prot)
{
  Local   *s = find(t, space, 0), *m = find(t, mem, LOCAL_MEM);
  Prepid   scope, destination;
  Handle   pid;
  Prepinfo info;
  Spaceid  target;
  Mem      state;
  int      error;
  if (!s || !m || (s->subtype != LOCAL_SPACE && s->subtype != LOCAL_CONSTRUCT))
    return -ERR_BAD_HANDLE;
  if (!(prot & VM_READ) || prot & ~7UL || ((prot & VM_WRITE) && (prot & VM_EXEC)))
    return -ERR_INVALID;
  if (prot & ~m->rights)
    return -ERR_DENIED;
  error = allowed(t, m, offset, length);
  if (error)
    return error;
  target = t->space;
  scope.generation = 0;
  scope.index = scope.reserved = 0;
  if (m->source == LOCAL_LOAD)
    scope = loadscope(m);
  if (s->subtype == LOCAL_CONSTRUCT) {
    error = localtarget(t, space, PREP_MAP, &destination, &pid);
    if (error)
      return error;
    if (!scope.generation || scope.generation != destination.generation ||
        scope.index != destination.index)
      return -ERR_DENIED;
    error = prepcheck(destination, t, &info);
    if (error)
      return error;
    target = info.space;
  } else if (!(s->rights & 1))
    return -ERR_DENIED;
  if (memstat(memory(m), &state))
    return -ERR_STALE;
  if (state.busy)
    return -ERR_BUSY;
  if ((state.state == MEM_SEALED && prot & VM_WRITE) ||
      (state.state != MEM_SEALED && prot & VM_EXEC))
    return -ERR_DENIED;
  if (s->subtype == LOCAL_SPACE && scope.generation)
    error = spaceloadmap(target, memory(m), offset, address, length, prot, scope);
  else
    error = spacemap(target, memory(m), offset, address, length, prot);
  if (error == SP_CONFLICT)
    return -ERR_STATE;
  return convert(error);
}
int
localunmap(Rtable *t, Handle space, u64 address)
{
  Local   *s = find(t, space, 0);
  Prepid   id;
  Prepinfo info;
  Handle   pid;
  int      error;
  if (!s)
    return -ERR_BAD_HANDLE;
  if (s->subtype == LOCAL_CONSTRUCT) {
    error = localtarget(t, space, PREP_MAP, &id, &pid);
    if (error)
      return error;
    error = prepcheck(id, t, &info);
    if (error)
      return error;
    return convert(spaceunmap(info.space, address));
  }
  if (s->subtype != LOCAL_SPACE)
    return -ERR_BAD_HANDLE;
  if (!(s->rights & 2))
    return -ERR_DENIED;
  return convert(spaceunmap(t->space, address));
}
int
localwrite(Rtable *t, Handle h, u64 offset, const void *data, u64 bytes)
{
  Local *l = find(t, h, LOCAL_MEM);
  Mem    state;
  int    error;
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & VM_WRITE))
    return -ERR_DENIED;
  error = allowed(t, l, offset, bytes);
  if (error)
    return error;
  if (memstat(memory(l), &state))
    return -ERR_STALE;
  if (state.state == MEM_SEALED)
    return -ERR_DENIED;
  return convert(memwrite(memory(l), offset, data, bytes));
}
int
localseal(Rtable *t, Handle h)
{
  Local *l = find(t, h, LOCAL_MEM);
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & LOCAL_SEAL))
    return -ERR_DENIED;
  if (l->source == LOCAL_LOAD) {
    int error = prepcheck(loadscope(l), t, NULL);
    if (error)
      return error;
  }
  return convert(memseal(memory(l)));
}
static int
removeentry(Local *l)
{
  int    error;
  Prepid scope;
  scope.generation = 0;
  scope.index = scope.reserved = 0;
  if (l->subtype == LOCAL_CONSTRUCT) {
    scope.generation = l->objectgen;
    scope.index = l->object;
    l->subtype = 0;
    l->rights = 0;
    if (prepcancel(scope) || prepdrop(scope))
      panic("construction local release");
    return 0;
  }
  if (l->subtype == LOCAL_MEM) {
    if (l->source == LOCAL_LOAD)
      scope = loadscope(l);
    error = memfree(memory(l));
  } else if (l->subtype == LOCAL_SPACE) {
    Spaceid id;
    id.generation = l->objectgen;
    id.index = l->object;
    id.reserved = 0;
    error = spacedrop(id);
  } else if (l->subtype == LOCAL_ALLOC) {
    Acctid id;
    id.generation = l->objectgen;
    id.index = l->object;
    id.reserved = 0;
    error = acctdrop(id);
  } else
    return -ERR_INVALID;
  if (error)
    return convert(error);
  l->subtype = 0;
  l->rights = 0;
  if (scope.generation && prepdrop(scope))
    panic("loading local scope release");
  return 0;
}
int
localfree(Rtable *t, Handle h)
{
  Local *l = find(t, h, LOCAL_MEM);
  return l ? removeentry(l) : -ERR_BAD_HANDLE;
}
int
localrevoke(Rtable *t, Handle h)
{
  Local *l = find(t, h, 0);
  return l ? removeentry(l) : -ERR_BAD_HANDLE;
}
int
localclose(Rtable *t)
{
  if (!t || !t->state)
    return -ERR_INVALID;
  if (t->state == LOCAL_OPEN) {
    t->state = LOCAL_CLOSING;
    prepownerclose(t);
  }
  return 0;
}
int
localdrain(Rtable *t, u32 count)
{
  u32 end;
  int error;
  if (!t || t->state == LOCAL_OPEN || !t->state || !count || count > 32)
    return -ERR_INVALID;
  end = MIN(t->count, t->cursor + count);
  while (t->cursor < end) {
    if (t->slots[t->cursor].subtype) {
      error = removeentry(&t->slots[t->cursor]);
      if (error)
        return error;
    }
    t->cursor++;
  }
  if (t->cursor == t->count) {
    t->state = LOCAL_DEAD;
    prepsourceclosed(t);
  }
  return t->state == LOCAL_DEAD ? 0 : 1;
}

void
localclearscope(Rtable *t, Prepid id, u32 first, u32 count)
{
  Local *l;
  Prepid scope;
  u32    i;
  if (first > t->count || count > t->count - first)
    panic("local scope cleanup bound");
  for (i = first; i < first + count; i++) {
    l = &t->slots[i];
    if (l->subtype == LOCAL_CONSTRUCT) {
      scope.generation = l->objectgen;
      scope.index = l->object;
      scope.reserved = 0;
    } else if (l->subtype == LOCAL_MEM && l->source == LOCAL_LOAD)
      scope = loadscope(l);
    else
      continue;
    if (scope.generation == id.generation && scope.index == id.index && removeentry(l))
      panic("local scope cleanup");
  }
}
