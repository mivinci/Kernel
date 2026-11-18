#include <local.h>
#include <diag.h>
#include <prep.h>
#include <rq.h>
#include <observe.h>
STATIC_ASSERT(AC_SETS <= 256 && LOCALS <= 255 && PENDING_CALLS <= 255, create_policy_encoding);

static int
convert(int error)
{
  if (!error)
    return 0;
  if (error == MEM_DENIED)
    return -ERR_DENIED;
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
  return l->subtype && l->subtype < LOCAL_DRAFT && !(l->source & LOCAL_STAGED) &&
                 l->generation == handlegen(h) && (!subtype || l->subtype == subtype)
             ? l
             : NULL;
}
int
localnative(Rtable *t, Handle h)
{
  return find(t, h, 0) ? 0 : -ERR_BAD_HANDLE;
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
int
localrooms(Rtable *t, u32 count)
{
  u32 i, available = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!count || count > LOCALS)
    return -ERR_INVALID;
  for (i = 0; i < t->count; i++)
    if (!t->slots[i].subtype && t->slots[i].generation < HANDLE_GENERATION_MAX)
      available++;
  return available >= count ? 0 : sloterror(t);
}
static int
ownedimage(Rtable *table, Imageid id, Image *out)
{
  if (!table || !out || imagestat(id, out) || out->pid != table->owner ||
      out->number != table->image)
    return -ERR_STALE;
  return 0;
}
static int
reserve(Rtable *table, Imageid id, u32 count, Rtable *lineage)
{
  Image image;
  u32   i, available = 0, floors = 0;
  int   error;
  if (!table || table->state != LOCAL_OPEN || count > 8 ||
      (lineage &&
       (lineage == table || lineage->count != table->count || lineage->state != LOCAL_OPEN)))
    return -ERR_INVALID;
  error = ownedimage(table, id, &image);
  if (error)
    return error;
  if (image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  for (i = 0; i < table->count; i++) {
    Local *slot = &table->slots[i];
    if (slot->subtype == LOCAL_RECEIVE)
      return -ERR_BUSY;
    if (slot->subtype == LOCAL_FLOOR)
      floors++;
    if ((!slot->subtype || slot->subtype == LOCAL_FLOOR) &&
        (lineage ? lineage->slots[i].generation : slot->generation) < HANDLE_GENERATION_MAX)
      available++;
  }
  if (floors != image.reserve)
    return -ERR_STATE;
  if (available < count) {
    if (lineage) {
      for (i = 0; i < table->count; i++)
        if ((!table->slots[i].subtype || table->slots[i].subtype == LOCAL_FLOOR) &&
            lineage->slots[i].generation == HANDLE_GENERATION_MAX)
          return -ERR_OVERFLOW;
      return -ERR_CAPACITY;
    }
    return sloterror(table);
  }
  if (imagereserve(id, count))
    return -ERR_STATE;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == LOCAL_FLOOR)
      table->slots[i].subtype = 0;
  for (i = 0; i < table->count && count; i++) {
    Local *slot = &table->slots[i];
    if (!slot->subtype &&
        (lineage ? lineage->slots[i].generation : slot->generation) < HANDLE_GENERATION_MAX) {
      slot->subtype = LOCAL_FLOOR;
      slot->source = lineage ? LOCAL_STAGED : 0;
      count--;
    }
  }
  return 0;
}
int
localreserve(Rtable *table, Imageid id, u32 count)
{
  return reserve(table, id, count, NULL);
}
int
localforkreserve(Rtable *source, Rtable *target, Imageid image, u32 count)
{
  if (!source || !source->slots || source->count > LOCALS)
    return -ERR_INVALID;
  return reserve(target, image, count, source);
}
int
localreceive(Rtable *table, Imageid id, Handle rpc, u16 *out)
{
  Image  image;
  Local *slot = NULL;
  u32    i, floor = 0;
  int    error;
  if (!out || !table || table->state != LOCAL_OPEN || handletype(rpc) != HANDLE_RPC)
    return -ERR_INVALID;
  error = ownedimage(table, id, &image);
  if (error)
    return error;
  if (image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == LOCAL_FLOOR) {
      slot = &table->slots[i];
      floor = 1;
      break;
    }
  if (!slot)
    slot = empty(table);
  if (!slot)
    return sloterror(table);
  slot->objectgen = handlegen(rpc);
  slot->object = handleindex(rpc);
  slot->offset = table->epoch;
  slot->length = image.number;
  slot->rights = 0;
  slot->inherit = 0;
  slot->source = (u8) floor;
  slot->subtype = LOCAL_RECEIVE;
  *out = (u16) (slot - table->slots);
  return 0;
}
int
localreturn(Rtable *table, Imageid id, Handle rpc, u16 index)
{
  Image  image;
  Local *slot;
  u32    floor;
  if (!table || (table->state != LOCAL_OPEN && table->state != LOCAL_CLOSING) ||
      index >= table->count || handletype(rpc) != HANDLE_RPC || ownedimage(table, id, &image))
    return -ERR_STATE;
  slot = &table->slots[index];
  if (slot->subtype != LOCAL_RECEIVE || slot->objectgen != handlegen(rpc) ||
      slot->object != handleindex(rpc) || slot->offset != table->epoch ||
      slot->length != image.number)
    return -ERR_STATE;
  floor = slot->source;
  slot->objectgen = slot->rights = slot->offset = slot->length = 0;
  slot->object = slot->inherit = slot->source = 0;
  slot->subtype = floor ? LOCAL_FLOOR : 0;
  return 0;
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
  if (!rights || rights & ~(LOCAL_GRANT | 15UL) || inherit & ~3U || (offset | length) & PAGE_MASK ||
      !length || memstat(id, &m) || !m.managers || offset > m.bytes || length > m.bytes - offset)
    return -ERR_INVALID;
  if (m.kind == MEM_BOOT)
    return -ERR_DENIED;
  if ((m.state != MEM_SHARED && m.kind != MEM_MMIO &&
       (m.acct.index != t->acct.index || m.acct.generation != t->acct.generation)) ||
      (m.state != MEM_SHARED && (inherit & LOCAL_EXEC)))
    return -ERR_DENIED;
  if (m.kind == MEM_MMIO && (inherit || (rights & (VM_EXEC | LOCAL_SEAL))))
    return -ERR_DENIED;
  if (((m.state == MEM_DATA || m.state == MEM_SHARED) && rights & (VM_EXEC | LOCAL_SEAL)) ||
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
  if (m.kind == MEM_MMIO)
    return -ERR_DENIED;
  if (((m.state == MEM_DATA || m.state == MEM_SHARED) && rights & (VM_EXEC | LOCAL_SEAL)) ||
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
static int
prepared(Rtable *t, Handle h, u64 rights, Prepid *id, Prepinfo *info)
{
  Local *l = find(t, h, LOCAL_CONSTRUCT);
  int    error;
  if (!l)
    return -ERR_BAD_HANDLE;
  if ((l->rights & rights) != rights)
    return -ERR_DENIED;
  id->generation = l->objectgen;
  id->index = l->object;
  id->reserved = 0;
  error = prepcheck(*id, t, info);
  if (error) {
    if (rights == PREP_PUBLISH && error == -ERR_REVOKED && preppublishcheck(*id) == -ERR_TIMEDOUT)
      return -ERR_TIMEDOUT;
    return -ERR_BAD_HANDLE;
  }
  return 0;
}
int
localcreate(Rtable *t, const Createpolicy *policy, Handle *out)
{
  Set    set;
  Local *l;
  u32    object;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!policy || !(policy->rights & CREATE_USE) ||
      policy->rights & ~(CREATE_RIGHTS | LOCAL_GRANT) || !policy->memlimit ||
      policy->exec_limit < policy->memlimit || policy->resources < 3 ||
      policy->resources > LOCALS || !policy->pending || policy->pending > PENDING_CALLS)
    return -ERR_INVALID;
  if (setstat(policy->set, &set))
    return -ERR_STALE;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = sethold(policy->set);
  if (error)
    return convert(error);
  /* Internal packing only; the public Handle remains the Local identity. */
  object = policy->set.index | (policy->resources << 8) | (policy->pending << 16);
  publish(t, l, LOCAL_CREATE, policy->set.generation, object, policy->rights, policy->memlimit,
          policy->exec_limit, 0, 0, out);
  return 0;
}
int
localcreator(Rtable *t, Handle h, Createpolicy *out)
{
  Local *l = find(t, h, LOCAL_CREATE);
  Set    set;
  u32    i;
  if (!out)
    return -ERR_INVALID;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = 0;
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & CREATE_USE))
    return -ERR_DENIED;
  out->set.generation = l->objectgen;
  out->set.index = l->object & 255;
  if (setstat(out->set, &set))
    return -ERR_STALE;
  out->memlimit = l->offset;
  out->exec_limit = l->length;
  out->resources = (l->object >> 8) & 255;
  out->pending = (l->object >> 16) & 255;
  out->rights = l->rights;
  return 0;
}
int
localcreatecheck(Rtable *t, Handle h, const Createarg *args, Createpolicy *out)
{
  int error;
  if (!args || args->reserved || !args->memlimit || args->exec_limit < args->memlimit ||
      args->resources < 3 || args->resources > LOCALS || !args->pending ||
      args->pending > PENDING_CALLS)
    return -ERR_INVALID;
  error = localcreator(t, h, out);
  if (error)
    return error;
  if (args->memlimit > out->memlimit || args->exec_limit > out->exec_limit ||
      args->resources > out->resources || args->pending > out->pending)
    return -ERR_DENIED;
  return 0;
}
int
localchildcreate(Rtable *source, Handle h, Rtable *target, const Createarg *args, Handle *out)
{
  Createpolicy policy;
  Acct         account;
  int          error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = localcreatecheck(source, h, args, &policy);
  if (error)
    return error;
  if (!target || target->state != LOCAL_OPEN || acctstat(target->acct, &account))
    return -ERR_BAD_HANDLE;
  if (account.set != policy.set.index)
    return -ERR_DENIED;
  policy.rights = CREATE_USE;
  policy.memlimit = args->memlimit;
  policy.exec_limit = args->exec_limit;
  policy.resources = args->resources;
  policy.pending = args->pending;
  return localcreate(target, &policy, out);
}
int
localtarget(Rtable *t, Handle h, u64 rights, Prepid *id, Handle *pid)
{
  Prepinfo info;
  int      error = prepared(t, h, rights, id, &info);
  if (error)
    return error;
  *pid = info.pid;
  return 0;
}
int
localqueryprep(Rtable *t, Handle h, Rtable **out)
{
  Prepid   id;
  Prepinfo info;
  int      error;
  if (!out)
    return -ERR_INVALID;
  *out = NULL;
  error = prepared(t, h, PREP_CONFIG, &id, &info);
  if (error)
    return error;
  /* Borrow only for the caller's masked-IRQ, non-scheduling snapshot. */
  *out = info.target;
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
static Imageid
endpoint(const Local *l)
{
  Imageid id;
  id.generation = l->objectgen;
  id.index = l->object;
  id.reserved = 0;
  return id;
}
int
localendpoint(Rtable *t, Imageid target, u32 kind, u64 rights, u64 bits, u32 inherit, Handle *out)
{
  Image  image;
  Local *l;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if ((kind != LOCAL_CALL && kind != LOCAL_SEND && kind != LOCAL_NOTIFY) || !(rights & LOCAL_USE) ||
      rights & ~(LOCAL_USE | LOCAL_GRANT) || inherit & ~3U ||
      (kind == LOCAL_NOTIFY ? (!bits || bits & 0xffffffffUL) : bits != 0))
    return -ERR_INVALID;
  if (imagestat(target, &image))
    return -ERR_STALE;
  if (image.state == IMAGE_ENDED)
    return -ERR_PEER;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = imagehold(target, IMAGE_CLEANUP);
  if (error)
    return error;
  publish(t, l, kind, target.generation, target.index, rights, bits, 0, inherit, 3, out);
  return 0;
}
int
localchannel(Rtable *t, Handle h, u32 kind, Imageid *target, u64 *bits)
{
  Local  *l;
  Image   image;
  Imageid id;
  if (!target || !bits || (kind != LOCAL_CALL && kind != LOCAL_SEND && kind != LOCAL_NOTIFY))
    return -ERR_INVALID;
  l = find(t, h, kind);
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & LOCAL_USE))
    return -ERR_DENIED;
  id = endpoint(l);
  if (imagestat(id, &image) || image.state == IMAGE_ENDED)
    return -ERR_PEER;
  if (image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  target->generation = id.generation;
  target->index = id.index;
  target->reserved = 0;
  *bits = l->offset;
  return 0;
}
static Lifeid
controlled(const Local *l)
{
  Lifeid id;
  id.generation = l->objectgen;
  id.index = l->object;
  id.reserved = 0;
  return id;
}
int
localcontrol(Rtable *t, Lifeid target, u64 rights, Handle *out)
{
  Local *l;
  Life   life;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!t || t->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (!(rights & CONTROL_RIGHTS) || rights & ~(CONTROL_RIGHTS | LOCAL_GRANT))
    return -ERR_INVALID;
  if (lifestat(target, &life))
    return -ERR_STALE;
  l = empty(t);
  if (!l)
    return sloterror(t);
  error = lifehold(target, LIFE_CONTROL);
  if (error)
    return error;
  publish(t, l, LOCAL_CONTROL, target.generation, target.index, rights, 0, 0, 0, 3, out);
  return 0;
}
int
localcontrolled(Rtable *t, Handle h, u64 rights, Lifeid *life, u64 *available)
{
  Local *l = find(t, h, LOCAL_CONTROL);
  Life   state;
  Lifeid id;
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!life || !available || !rights || rights & ~CONTROL_RIGHTS)
    return -ERR_INVALID;
  if ((l->rights & rights) != rights)
    return -ERR_DENIED;
  id = controlled(l);
  if (lifestat(id, &state))
    return -ERR_STALE;
  life->generation = id.generation;
  life->index = id.index;
  life->reserved = 0;
  *available = l->rights;
  return 0;
}
int
localobserve(Rtable *table, u64 rights, Handle *out)
{
  Local  *slot;
  Imageid owner;
  int     error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!(rights & OBSERVE_RIGHTS) || rights & ~((u64) OBSERVE_RIGHTS | LOCAL_GRANT))
    return -ERR_INVALID;
  slot = empty(table);
  if (!slot)
    return table && table->state == LOCAL_OPEN ? sloterror(table) : -ERR_STATE;
  error = imagefind(table->owner, table->image, &owner);
  if (error)
    return error;
  error = obsreserve(owner);
  if (error)
    return error;
  publish(table, slot, LOCAL_OBSERVE, owner.generation, owner.index, rights, 0, 0, 0, 0, out);
  return 0;
}
static Irqid
bindingid(const Local *local)
{
  Irqid id;
  id.generation = local->objectgen;
  id.index = local->object;
  id.reserved = 0;
  return id;
}
int
localirqsource(Rtable *table, u32 number, Handle *out)
{
  Local *slot;
  u32    source;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!table || table->state != LOCAL_OPEN)
    return -ERR_STATE;
  error = irqsource(number, &source);
  if (error)
    return error;
  slot = empty(table);
  if (!slot)
    return sloterror(table);
  /* Immutable inventory indices have no ref or binding ownership. */
  publish(table, slot, LOCAL_IRQ_SOURCE, 0, source, IRQ_SOURCE_BIND, 0, 0, 0, 0, out);
  return 0;
}
int
localirqresolve(Rtable *table, Handle handle, u32 *source)
{
  Local *slot = find(table, handle, LOCAL_IRQ_SOURCE);
  if (!source)
    return -ERR_INVALID;
  *source = 0;
  if (!slot)
    return -ERR_BAD_HANDLE;
  if (!(slot->rights & IRQ_SOURCE_BIND))
    return -ERR_DENIED;
  *source = slot->object;
  return 0;
}
int
localirqbind(Rtable *table, Imageid image, Event *event, u32 source, u32 bit, Handle *out)
{
  Local *slot;
  Image  target;
  Irqid  id;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!table || table->state != LOCAL_OPEN)
    return -ERR_STATE;
  if (ownedimage(table, image, &target))
    return -ERR_STALE;
  slot = empty(table);
  if (!slot)
    return sloterror(table);
  error = irqnew(source, image, event, bit, &id);
  if (error)
    return error;
  publish(table, slot, LOCAL_IRQ_BIND, id.generation, id.index, IRQ_BIND_ACK, 0, 0, 0, 0, out);
  return 0;
}
int
localirqack(Rtable *table, Handle handle, Imageid image)
{
  Local *slot = find(table, handle, LOCAL_IRQ_BIND);
  Image  target;
  if (!slot)
    return -ERR_BAD_HANDLE;
  if (!(slot->rights & IRQ_BIND_ACK))
    return -ERR_DENIED;
  if (ownedimage(table, image, &target))
    return -ERR_DENIED;
  return irqack(bindingid(slot), image);
}
int
localirqunbind(Rtable *table, Handle handle, Imageid image)
{
  Local *slot = find(table, handle, LOCAL_IRQ_BIND);
  Image  target;
  int    error;
  if (!slot)
    return -ERR_BAD_HANDLE;
  if (ownedimage(table, image, &target))
    return -ERR_DENIED;
  error = irqcancel(bindingid(slot), image);
  if (error)
    return error;
  if (irqdrop(bindingid(slot)))
    panic("IRQ local release");
  slot->subtype = 0;
  slot->rights = 0;
  return 0;
}
int
localobserver(Rtable *table, Handle permit, u32 relation)
{
  Local  *slot = find(table, permit, LOCAL_OBSERVE);
  Imageid owner;
  Image   image;
  if (!slot)
    return -ERR_BAD_HANDLE;
  if (relation > OBS_MONITOR)
    return -ERR_INVALID;
  if (!(slot->rights & ((u64) 1 << relation)))
    return -ERR_DENIED;
  owner.generation = slot->objectgen;
  owner.index = slot->object;
  owner.reserved = 0;
  if (imagestat(owner, &image) || image.pid != table->owner || image.number != table->image)
    return -ERR_STALE;
  return 0;
}
int
localcopy(Rtable *source, Handle h, Rtable *target, u64 rights, u64 offset, u64 length, u32 inherit,
          Handle *out)
{
  Local   *l = find(source, h, 0);
  Prepinfo info;
  Mem      backing;
  int      error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!l)
    return -ERR_BAD_HANDLE;
  if (!(l->rights & LOCAL_GRANT) || rights & ~l->rights)
    return -ERR_DENIED;
  if (l->subtype == LOCAL_OBSERVE) {
    if (offset || length || inherit)
      return -ERR_INVALID;
    return localobserve(target, rights, out);
  }
  if (l->subtype == LOCAL_CREATE)
    return -ERR_NOTSUP; /* Delegated CREATE policy still needs SERVICE/POLICY admission. */
  if (l->subtype == LOCAL_CONTROL) {
    if (offset || length || inherit)
      return -ERR_INVALID;
    return localcontrol(target, controlled(l), rights, out);
  }
  if (l->subtype == LOCAL_CALL || l->subtype == LOCAL_SEND || l->subtype == LOCAL_NOTIFY) {
    if (offset || length)
      return -ERR_INVALID;
    if (inherit & ~l->inherit)
      return -ERR_DENIED;
    return localendpoint(target, endpoint(l), l->subtype, rights, l->offset, inherit, out);
  }
  if (l->subtype != LOCAL_MEM)
    return -ERR_BAD_HANDLE;
  error = allowed(source, l, offset, length);
  if (error)
    return error;
  if (l->source == LOCAL_LOAD) {
    error = prepcheck(loadscope(l), source, &info);
    if (error)
      return error;
    if (target != info.target)
      return -ERR_DENIED;
    if (memstat(memory(l), &backing))
      return -ERR_STALE;
    if (backing.kind == MEM_MMIO && inherit)
      return -ERR_DENIED;
    if (inherit & ~(backing.state == MEM_SHARED ? 3U : LOCAL_FORK))
      return -ERR_INVALID;
  } else if (inherit & ~l->inherit)
    return -ERR_DENIED;
  error = localmem(target, memory(l), rights, offset, length, inherit, out);
  if (!error)
    target->slots[handleindex(*out)].source = 3;
  return error;
}
int
localreplycheck(Rtable *source, u32 kind, const Resreply *reply)
{
  Local *entry;
  Mem    backing;
  Image  image;
  if (!reply)
    return -ERR_INVALID;
  entry = find(source, reply->source, 0);
  if (!entry)
    return -ERR_BAD_HANDLE;
  if (!(entry->rights & LOCAL_GRANT) || reply->rights & ~entry->rights || entry->subtype != kind)
    return -ERR_DENIED;
  if (kind == LOCAL_MEM) {
    if (entry->source == LOCAL_LOAD ||
        (reply->rights != VM_READ && reply->rights != (VM_READ | VM_WRITE)))
      return -ERR_DENIED;
    if (!reply->length || (reply->offset | reply->length) & PAGE_MASK)
      return -ERR_INVALID;
    if (!window(entry, reply->offset, reply->length))
      return -ERR_DENIED;
    if (memstat(memory(entry), &backing))
      return -ERR_STALE;
    if (backing.state != MEM_SHARED || !backing.managers)
      return -ERR_DENIED;
  } else if (kind == LOCAL_CALL) {
    if (reply->rights != LOCAL_USE)
      return -ERR_DENIED;
    if (reply->offset || reply->length)
      return -ERR_INVALID;
    if (imagestat(endpoint(entry), &image))
      return -ERR_STALE;
    if (image.state == IMAGE_ENDED)
      return -ERR_PEER;
  } else
    return -ERR_DENIED;
  return 0;
}
int
localdeliver(Rtable *source, u32 kind, const Resreply *reply, Rtable *target, Imageid id,
             Handle rpc, u16 index, Handle *out)
{
  Local *entry, *slot;
  Image  image;
  u32    floor;
  int    error;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  error = localreplycheck(source, kind, reply);
  if (error)
    return error;
  if (!target || target->state != LOCAL_OPEN || index >= target->count ||
      handletype(rpc) != HANDLE_RPC || ownedimage(target, id, &image) ||
      image.state != IMAGE_ACTIVE)
    return -ERR_STATE;
  slot = &target->slots[index];
  if (slot->subtype != LOCAL_RECEIVE || slot->objectgen != handlegen(rpc) ||
      slot->object != handleindex(rpc) || slot->offset != target->epoch ||
      slot->length != image.number)
    return -ERR_STATE;
  if (slot->generation == HANDLE_GENERATION_MAX)
    return -ERR_OVERFLOW;
  floor = slot->source;
  if (floor && !image.reserve)
    return -ERR_STATE;
  entry = find(source, reply->source, kind);
  /* Ref acquisition is the last fallible step. No IRQ/checkpoint can change
     the exact request slot or either table between validation and publication. */
  error = kind == LOCAL_MEM ? convert(memhold(memory(entry)))
                            : imagehold(endpoint(entry), IMAGE_CLEANUP);
  if (error)
    return error;
  if (floor && imageusefloor(id))
    panic("resource reply floor ownership");
  publish(target, slot, kind, entry->objectgen, entry->object, reply->rights, reply->offset,
          reply->length, LOCAL_FORK, 3, out);
  return 0;
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
  if (a->kind == ALLOC_DMA)
    return -ERR_NOTSUP;
  if (a->bytes > ~(u64) 0 - PAGE_MASK)
    return -ERR_OVERFLOW;
  if (acctstat(payer, &account) || account.state != AC_ACTIVE)
    return -ERR_STATE;
  if (a->bytes > (u64) MEM_PAGES * PAGE_SIZE)
    return -ERR_NOTSUP;
  if (!empty(t))
    return sloterror(t);
  if (a->kind == ALLOC_CODE)
    error = memcode(payer, a->bytes, &id);
  else if (a->kind == ALLOC_SHARED)
    error = memshared(payer, a->bytes, &id);
  else
    error = memcreate(payer, a->bytes, &id);
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
  if (l->subtype == LOCAL_FLOOR) {
    l->subtype = 0;
    return 0;
  }
  if (l->subtype == LOCAL_RECEIVE)
    return -ERR_BUSY;
  if (l->subtype == LOCAL_IRQ_SOURCE) {
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_IRQ_BIND) {
    if (irqdrop(bindingid(l)))
      panic("IRQ local reference");
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_CREATE) {
    Setid set;
    set.generation = l->objectgen;
    set.index = l->object & 255;
    set.reserved = 0;
    error = setdrop(set);
    if (error)
      return convert(error);
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_CONSTRUCT) {
    scope.generation = l->objectgen;
    scope.index = l->object;
    l->subtype = 0;
    l->rights = 0;
    if (prepcancel(scope) || prepdrop(scope))
      panic("construction local release");
    return 0;
  }
  if (l->subtype == LOCAL_OBSERVE) {
    Imageid owner;
    owner.generation = l->objectgen;
    owner.index = l->object;
    owner.reserved = 0;
    if (obsrelease(owner))
      panic("local observer reservation");
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_CONTROL) {
    if (lifedrop(controlled(l), LIFE_CONTROL))
      panic("local control reference");
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_CALL || l->subtype == LOCAL_SEND || l->subtype == LOCAL_NOTIFY) {
    if (imagedrop(endpoint(l), IMAGE_CLEANUP))
      panic("local endpoint reference");
    l->subtype = 0;
    l->rights = 0;
    return 0;
  }
  if (l->subtype == LOCAL_MEM || l->subtype == LOCAL_DRAFT) {
    if (l->subtype == LOCAL_MEM && l->source == LOCAL_LOAD)
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
localinherit(Rtable *t, Handle h, u64 flags)
{
  Local *l;
  int    error;
  if (flags & ~3UL)
    return -ERR_INVALID;
  error = localnative(t, h);
  if (error)
    return error;
  l = find(t, h, 0);
  if (l->subtype == LOCAL_MEM) {
    Mem backing;
    if (memstat(memory(l), &backing))
      return -ERR_STALE;
    if (backing.kind == MEM_MMIO && flags)
      return -ERR_DENIED;
    if ((flags & LOCAL_EXEC) && backing.state != MEM_SHARED)
      return -ERR_DENIED;
  } else if (l->subtype != LOCAL_CALL && l->subtype != LOCAL_SEND && l->subtype != LOCAL_NOTIFY)
    return -ERR_DENIED;
  l->inherit = (u8) flags;
  return 0;
}
int
localfree(Rtable *t, Handle h)
{
  Local *l = find(t, h, 0);
  return l && (l->subtype == LOCAL_MEM || l->subtype == LOCAL_CALL || l->subtype == LOCAL_SEND ||
               l->subtype == LOCAL_NOTIFY || l->subtype == LOCAL_CONTROL ||
               l->subtype == LOCAL_IRQ_BIND || l->subtype == LOCAL_IRQ_SOURCE)
             ? removeentry(l)
             : -ERR_BAD_HANDLE;
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
    t->slots[t->cursor].source &= ~LOCAL_STAGED;
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

static int
queryrecord(const Rtable *table, u32 index, Queryrec *out)
{
  const Local *l = &table->slots[index];
  Mem          m;
  Image        image;
  u32          i;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = 0;
  out->type = QUERY_RESOURCE;
  out->pid = table->owner;
  out->image = table->image;
  out->stamp = l->generation;
  out->data[0] = (u64) handlepack(HANDLE_RESOURCE, l->generation, index);
  out->data[1] = l->subtype;
  out->data[2] = l->rights;
  out->data[5] = l->inherit;
  if (l->subtype == LOCAL_MEM) {
    if (l->source == LOCAL_LOAD) {
      if (memstat(memory(l), &m))
        return -ERR_STALE;
      out->data[4] = m.bytes;
    } else {
      out->data[3] = l->offset;
      out->data[4] = l->length;
    }
  } else if (l->subtype == LOCAL_CALL || l->subtype == LOCAL_SEND || l->subtype == LOCAL_NOTIFY) {
    if (imagestat(endpoint(l), &image))
      return -ERR_STALE;
    out->data[6] = (u64) image.pid;
    out->data[7] = image.number;
  } else if (l->subtype != LOCAL_SPACE && l->subtype != LOCAL_ALLOC &&
             l->subtype != LOCAL_CONTROL && l->subtype != LOCAL_CONSTRUCT &&
             l->subtype != LOCAL_CREATE && l->subtype != LOCAL_OBSERVE &&
             l->subtype != LOCAL_IRQ_SOURCE && l->subtype != LOCAL_IRQ_BIND)
    return -ERR_NOTSUP;
  return 0;
}
int
localquery(Rtable *table, u64 cursor, Queryrec *records, u32 count, u32 *written, u64 *next)
{
  u32    index = 0, n = 0;
  Local *l;
  int    error;
  if (!written || !next)
    return -ERR_INVALID;
  *written = 0;
  *next = 0;
  if (!table || !records || !count || count > QUERY_RECORDS || cursor >> 59)
    return -ERR_INVALID;
  if (table->state != LOCAL_OPEN && table->state != LOCAL_CLOSING)
    return -ERR_STATE;
  if (cursor) {
    index = (u32) (cursor & 65535);
    if (!(cursor >> 16) || index >= table->count)
      return -ERR_INVALID;
    l = &table->slots[index];
    if (!l->subtype || l->subtype >= LOCAL_DRAFT || l->source & LOCAL_STAGED ||
        l->generation != cursor >> 16)
      return -ERR_STALE;
  }
  for (; index < table->count; index++) {
    l = &table->slots[index];
    if (!l->subtype || l->subtype >= LOCAL_DRAFT || l->source & LOCAL_STAGED)
      continue;
    if (n == count) {
      *next = (l->generation << 16) | index;
      break;
    }
    error = queryrecord(table, index, &records[n]);
    if (error) {
      *next = 0;
      return error;
    }
    n++;
  }
  *written = n;
  return 0;
}

static int
sameid(Memid a, Memid b)
{
  return a.index == b.index && a.generation == b.generation && !a.reserved && !b.reserved;
}
static void
clearforkslot(Local *slot)
{
  slot->objectgen = slot->rights = slot->offset = slot->length = 0;
  slot->object = slot->subtype = slot->inherit = slot->source = 0;
}
static void
copyforkslot(Local *to, const Local *from)
{
  to->generation = from->generation;
  to->objectgen = from->objectgen;
  to->rights = from->rights;
  to->offset = from->offset;
  to->length = from->length;
  to->object = from->object;
  to->subtype = from->subtype;
  to->inherit = from->inherit;
  to->source = from->source;
}
static const Forkbinding *
forkbinding(const Forkbinding *bindings, u32 count, Memid id)
{
  u32 i;
  for (i = 0; i < count; i++)
    if (sameid(bindings[i].source, id))
      return &bindings[i];
  return NULL;
}
static int
forkmem(Rtable *source, Rtable *target, const Local *slot, const Forkbinding *binding)
{
  Mem old, view;
  if (!binding || slot->source == LOCAL_LOAD || !sameid(binding->source, memory(slot)) ||
      memstat(binding->source, &old) || memstat(binding->target, &view) || !old.managers ||
      !view.refs || old.bytes != view.bytes || old.pages != view.pages || old.state != view.state ||
      old.kind == MEM_MMIO || view.kind == MEM_MMIO || (slot->rights & ~(LOCAL_GRANT | 15UL)) ||
      !slot->rights || slot->inherit & ~3U || ((slot->offset | slot->length) & PAGE_MASK) ||
      !slot->length || slot->offset > view.bytes || slot->length > view.bytes - slot->offset)
    return -ERR_INVALID;
  if (old.busy || view.busy)
    return -ERR_BUSY;
  if (old.state == MEM_SHARED)
    return old.kind == MEM_RAM && sameid(binding->source, binding->target) ? 0 : -ERR_DENIED;
  if ((old.state != MEM_DATA && old.state != MEM_SEALED) || old.acct.index != source->acct.index ||
      old.acct.generation != source->acct.generation || view.acct.index != target->acct.index ||
      view.acct.generation != target->acct.generation || sameid(binding->source, binding->target) ||
      (old.state == MEM_DATA && view.kind != MEM_RAM) ||
      (old.state == MEM_SEALED && view.kind != MEM_IMMUTABLE))
    return -ERR_DENIED;
  return 0;
}
static int
forktables(Rtable *source, Rtable *target, const Createarg *limits, u32 staged)
{
  Acct    parent, child;
  Space   parentspace, childspace;
  Image   image;
  Imageid childimage;
  u32     i;
  if (!source || !target || source == target || !source->slots || !target->slots ||
      source->state != LOCAL_OPEN || target->state != LOCAL_OPEN || !source->count ||
      source->count > LOCALS || target->count != source->count || source->slots == target->slots ||
      source->count < 3 ||
      (limits && (limits->reserved || limits->resources != target->count || !limits->pending ||
                  limits->pending > PENDING_CALLS)) ||
      handletype(source->owner) != HANDLE_PROCESS || handletype(target->owner) != HANDLE_PROCESS ||
      source->owner == target->owner || !source->image || !target->image ||
      acctstat(source->acct, &parent) || acctstat(target->acct, &child) ||
      parent.state != AC_ACTIVE || child.state != AC_ACTIVE || parent.set != child.set ||
      (source->acct.index == target->acct.index &&
       source->acct.generation == target->acct.generation) ||
      (limits && (child.limit != limits->memlimit || child.exec_limit != limits->exec_limit)) ||
      spacestat(source->space, &parentspace) || spacestat(target->space, &childspace) ||
      parentspace.acct.index != source->acct.index ||
      parentspace.acct.generation != source->acct.generation ||
      childspace.acct.index != target->acct.index ||
      childspace.acct.generation != target->acct.generation)
    return -ERR_INVALID;
  if (imagefind(source->owner, source->image, &childimage) || imagestat(childimage, &image) ||
      image.state != IMAGE_ACTIVE || imagefind(target->owner, target->image, &childimage) ||
      imagestat(childimage, &image) || image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  if ((u64) source->slots < (u64) target->slots + target->count * sizeof(Local) &&
      (u64) target->slots < (u64) source->slots + source->count * sizeof(Local))
    return -ERR_INVALID;

  for (i = 0; i < target->count; i++)
    if (target->slots[i].generation > HANDLE_GENERATION_MAX ||
        (target->slots[i].subtype && (!staged || target->slots[i].subtype != LOCAL_DRAFT ||
                                      target->slots[i].source != LOCAL_STAGED)))
      return -ERR_STATE;
  return 0;
}
int
localforkview(Rtable *target, Memid original, Memid *out)
{
  Local *slot;
  u32    i;
  if (!target || !out || target->state != LOCAL_OPEN || !target->slots || target->count > LOCALS ||
      original.reserved)
    return -ERR_INVALID;
  for (i = 0; i < target->count; i++) {
    slot = &target->slots[i];
    if (slot->subtype == LOCAL_DRAFT && slot->source == LOCAL_STAGED &&
        slot->rights == original.generation && slot->offset == original.index) {
      *out = memory(slot);
      return 0;
    }
  }
  return -ERR_STALE;
}
int
localforkbind(Rtable *source, Rtable *target, u32 index, Memid view)
{
  Forkbinding binding;
  Local      *slot, *draft;
  Memid       existing;
  u32         i;
  int         error = forktables(source, target, NULL, 1);
  if (error)
    return error;
  if (index >= source->count)
    return -ERR_INVALID;
  slot = &source->slots[index];
  draft = &target->slots[index];
  if (slot->subtype != LOCAL_MEM || !(slot->inherit & LOCAL_FORK) || slot->source & LOCAL_STAGED ||
      draft->subtype)
    return -ERR_STATE;
  binding.source = memory(slot);
  binding.target = view;
  error = forkmem(source, target, slot, &binding);
  if (error)
    return error;
  if (!localforkview(target, binding.source, &existing))
    return -ERR_STATE;
  for (i = 0; i < target->count; i++)
    if (target->slots[i].subtype == LOCAL_DRAFT && sameid(memory(&target->slots[i]), view))
      return -ERR_INVALID;
  error = convert(memforkhold(view, target->acct));
  if (error)
    return error;
  clearforkslot(draft);
  draft->objectgen = view.generation;
  draft->object = view.index;
  draft->rights = binding.source.generation;
  draft->offset = binding.source.index;
  draft->subtype = LOCAL_DRAFT;
  draft->source = LOCAL_STAGED;
  return 0;
}
int
localforkdrop(Rtable *target)
{
  Imageid image;
  Image   state;
  Local  *slot;
  u32     i, floors = 0;
  if (!target || !target->slots || target->count > LOCALS ||
      (target->state != LOCAL_OPEN && target->state != LOCAL_CLOSING))
    return -ERR_INVALID;
  for (i = 0; i < target->count; i++) {
    slot = &target->slots[i];
    if (!(slot->source & LOCAL_STAGED) && slot->subtype != LOCAL_DRAFT)
      continue;
    if (slot->subtype == LOCAL_FLOOR)
      floors++;
    if (slot->subtype && removeentry(slot))
      panic("fork staged reference release");
    clearforkslot(slot);
  }
  if (floors && !imagefind(target->owner, target->image, &image) && !imagestat(image, &state)) {
    if (state.state == IMAGE_PREPARING) {
      if (imagereserve(image, 0))
        panic("fork staged floor release");
    } else if (state.state != IMAGE_ENDED)
      panic("fork staged floor publication");
  }
  return 0;
}
int
localforkload(Rtable *source, Rtable *target, const Createarg *limits)
{
  Forkbinding  binding;
  Createpolicy policy;
  Acct         parent;
  Image        image;
  Local       *slot, *dest;
  Memid        view;
  u64          generation;
  u32          i;
  int          error;
  if (!limits)
    return -ERR_INVALID;
  error = forktables(source, target, limits, 1);
  if (error)
    return error;
  if (acctstat(source->acct, &parent))
    return -ERR_STATE;
  for (i = 0; i < target->count; i++)
    if (target->slots[i].subtype == LOCAL_DRAFT &&
        (source->slots[i].subtype != LOCAL_MEM || !(source->slots[i].inherit & LOCAL_FORK) ||
         target->slots[i].rights != source->slots[i].objectgen ||
         target->slots[i].offset != source->slots[i].object))
      return -ERR_INVALID;
  /* Entire source admission before taking new references. Manager handles
     remain scoped to their child's already prepared logical view. */
  for (i = 0; i < source->count; i++) {
    slot = &source->slots[i];
    if (slot->subtype && (slot->source & LOCAL_STAGED || slot->subtype == LOCAL_DRAFT))
      return -ERR_STATE;
    if (slot->generation > HANDLE_GENERATION_MAX ||
        (slot->subtype && slot->subtype < LOCAL_FLOOR && !slot->generation))
      return -ERR_INVALID;
    if (!slot->subtype || slot->subtype >= LOCAL_FLOOR)
      continue;
    switch (slot->subtype) {
    case LOCAL_SPACE:
      if (slot->objectgen != source->space.generation || slot->object != source->space.index ||
          !slot->rights || slot->rights & ~3UL || slot->offset || slot->length || slot->inherit)
        return -ERR_DENIED;
      break;
    case LOCAL_ALLOC:
      if (slot->objectgen != source->acct.generation || slot->object != source->acct.index ||
          !slot->rights || slot->rights & ~7UL || slot->offset || slot->length || slot->inherit)
        return -ERR_DENIED;
      break;
    case LOCAL_CREATE:
      error = localcreatecheck(source, handlepack(HANDLE_RESOURCE, slot->generation, i), limits,
                               &policy);
      if (error)
        return error;
      if (policy.set.index != parent.set)
        return -ERR_DENIED;
      break;
    case LOCAL_MEM:
      if (!(slot->inherit & LOCAL_FORK))
        break;
      binding.source = memory(slot);
      error = localforkview(target, binding.source, &binding.target);
      if (!error)
        error = forkmem(source, target, slot, &binding);
      if (error)
        return error;
      break;
    case LOCAL_CALL:
    case LOCAL_SEND:
    case LOCAL_NOTIFY:
      if (!(slot->inherit & LOCAL_FORK))
        break;
      if (!(slot->rights & LOCAL_USE) || slot->rights & ~(LOCAL_USE | LOCAL_GRANT) ||
          slot->inherit & ~3U || slot->length ||
          (slot->subtype == LOCAL_NOTIFY ? (!slot->offset || slot->offset & 0xffffffffUL)
                                         : slot->offset != 0))
        return -ERR_INVALID;
      error = imagestat(endpoint(slot), &image);
      if (error)
        return error;
      break;
    case LOCAL_CONTROL:
    case LOCAL_OBSERVE:
    case LOCAL_IRQ_SOURCE:
    case LOCAL_IRQ_BIND:
    case LOCAL_CONSTRUCT:
      break;
    default:
      return -ERR_NOTSUP;
    }
  }
  for (i = 0; i < source->count; i++) {
    slot = &source->slots[i];
    dest = &target->slots[i];
    if (!slot->subtype || slot->subtype >= LOCAL_FLOOR || dest->subtype == LOCAL_DRAFT)
      continue;
    error = 0;
    if (slot->subtype == LOCAL_SPACE)
      error = convert(spacehold(target->space));
    else if (slot->subtype == LOCAL_ALLOC)
      error = convert(accthold(target->acct));
    else if (slot->subtype == LOCAL_CREATE) {
      error = localcreator(source, handlepack(HANDLE_RESOURCE, slot->generation, i), &policy);
      if (!error)
        error = convert(sethold(policy.set));
    } else if (slot->subtype == LOCAL_MEM && (slot->inherit & LOCAL_FORK)) {
      binding.source = memory(slot);
      error = localforkview(target, binding.source, &binding.target);
      if (!error)
        error = convert(memforkhold(binding.target, target->acct));
    } else if ((slot->subtype == LOCAL_CALL || slot->subtype == LOCAL_SEND ||
                slot->subtype == LOCAL_NOTIFY) &&
               (slot->inherit & LOCAL_FORK)) {
      error = imagehold(endpoint(slot), IMAGE_CLEANUP);
    } else
      continue;
    if (error)
      goto failed;
    generation = dest->generation;
    copyforkslot(dest, slot);
    dest->generation = generation;
    dest->source |= LOCAL_STAGED;
    if (slot->subtype == LOCAL_SPACE) {
      dest->objectgen = target->space.generation;
      dest->object = target->space.index;
    } else if (slot->subtype == LOCAL_ALLOC) {
      dest->objectgen = target->acct.generation;
      dest->object = target->acct.index;
    } else if (slot->subtype == LOCAL_CREATE) {
      dest->rights = CREATE_USE;
      dest->offset = limits->memlimit;
      dest->length = limits->exec_limit;
      dest->object = policy.set.index | (limits->resources << 8) | (limits->pending << 16);
    } else if (slot->subtype == LOCAL_MEM) {
      dest->objectgen = binding.target.generation;
      dest->object = binding.target.index;
      dest->source = 3 | LOCAL_STAGED;
    }
  }

  /* Providers retain their original-ID lookup fields until all other aliases
     have acquired references. Then their existing manager becomes the MEM row. */
  for (i = 0; i < target->count; i++) {
    dest = &target->slots[i];
    if (dest->subtype != LOCAL_DRAFT)
      continue;
    view = memory(dest);
    generation = dest->generation;
    copyforkslot(dest, &source->slots[i]);
    dest->generation = generation;
    dest->objectgen = view.generation;
    dest->object = view.index;
    dest->source = 3 | LOCAL_STAGED;
  }
  return 0;
failed:
  if (localforkdrop(target))
    panic("fork staged table rollback");
  return error;
}
int
localforkfinish(Rtable *source, Rtable *target)
{
  Imageid identity;
  Image   image;
  Local  *slot;
  u32     i;
  if (!source || !target || source == target || source->state != LOCAL_OPEN ||
      target->state != LOCAL_OPEN || !source->slots || !target->slots ||
      source->count != target->count || source->count > LOCALS ||
      imagefind(source->owner, source->image, &identity) || imagestat(identity, &image) ||
      image.state != IMAGE_ACTIVE || imagefind(target->owner, target->image, &identity) ||
      imagestat(identity, &image) || image.state != IMAGE_PREPARING)
    return -ERR_STATE;
  for (i = 0; i < target->count; i++) {
    slot = &target->slots[i];
    if (source->slots[i].generation > HANDLE_GENERATION_MAX || slot->subtype == LOCAL_DRAFT ||
        (slot->subtype && !(slot->source & LOCAL_STAGED)) ||
        (slot->subtype == LOCAL_FLOOR && source->slots[i].generation == HANDLE_GENERATION_MAX))
      return -ERR_STATE;
  }
  for (i = 0; i < target->count; i++) {
    slot = &target->slots[i];
    if (!slot->subtype)
      clearforkslot(slot);
    slot->generation = source->slots[i].generation;
    slot->source &= ~LOCAL_STAGED;
  }
  return 0;
}
int
localfork(Rtable *source, Rtable *target, const Forkbinding *bindings, u32 count,
          const Createarg *limits)
{
  const Forkbinding *binding;
  Local             *slot;
  Memid              original, view;
  u32                i, j;
  int                error;
  if (!limits || count > LOCALS || (count && !bindings))
    return -ERR_INVALID;
  error = forktables(source, target, limits, 0);
  if (error)
    return error;
  for (i = 0; i < count; i++) {
    if (bindings[i].source.reserved || bindings[i].target.reserved)
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (sameid(bindings[i].source, bindings[j].source) ||
          sameid(bindings[i].target, bindings[j].target))
        return -ERR_INVALID;
  }
  for (i = 0; i < source->count; i++) {
    slot = &source->slots[i];
    if (slot->subtype != LOCAL_MEM || !(slot->inherit & LOCAL_FORK))
      continue;
    original = memory(slot);
    if (!localforkview(target, original, &view))
      continue;
    binding = forkbinding(bindings, count, original);
    error = binding ? localforkbind(source, target, i, binding->target) : -ERR_INVALID;
    if (error)
      goto failed;
  }
  error = localforkload(source, target, limits);
  if (!error)
    error = localforkfinish(source, target);
  if (!error)
    return 0;
failed:
  if (localforkdrop(target))
    panic("fork table staged rollback");
  return error;
}
