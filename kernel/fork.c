#include <fork.h>
#include <signal.h>
#include <hal/context.h>
#include <diag.h>

static int
convert(int error)
{
  if (!error)
    return 0;
  if (error == AC_LIMIT)
    return -ERR_LIMIT;
  if (error == AC_FULL)
    return -ERR_CAPACITY;
  if (error == AC_NOMEM)
    return -ERR_NOMEM;
  if (error == AC_BUSY)
    return -ERR_BUSY;
  if (error == MEM_DENIED)
    return -ERR_DENIED;
  return -ERR_INVALID;
}
static int
same(Memid a, Memid b)
{
  return a.generation == b.generation && a.index == b.index;
}
static Memid
memory(const Local *slot)
{
  Memid id;
  id.generation = slot->objectgen;
  id.index = slot->object;
  id.reserved = 0;
  return id;
}

int
forksnapshot(Rtable *source, Rtable *target, const Createarg *limits, u32 reserve,
             const Hctx *parent, Hctx *child)
{
  Forkbinding bindings[LOCALS];
  u64         generations[LOCALS];
  u32         adopted[LOCALS];
  u8          owned[LOCALS];
  Hctx        candidate;
  Imageid     sourceimage, targetimage;
  Image       image;
  Life        life;
  Mem         backing;
  Memid       original;
  Local      *slot;
  u32         i, j, count = 0, inherited = 0;
  int         error;
  if (!source || !target || source == target || !source->slots || !target->slots ||
      source->state != LOCAL_OPEN || target->state != LOCAL_OPEN || !source->count ||
      source->count > LOCALS || source->count != target->count || !limits || reserve > 8 ||
      !parent || !child ||
      ((u64) parent < (u64) child + sizeof(Hctx) && (u64) child < (u64) parent + sizeof(Hctx)))
    return -ERR_INVALID;
  if (imagefind(source->owner, source->image, &sourceimage) || imagestat(sourceimage, &image) ||
      image.state != IMAGE_ACTIVE || imagefind(target->owner, target->image, &targetimage) ||
      imagestat(targetimage, &image) || image.state != IMAGE_PREPARING || image.reserve ||
      lifestat(image.life, &life) || life.state != LIFE_CONSTRUCT || life.parent != source->owner)
    return -ERR_STATE;
  for (i = 0; i < target->count; i++) {
    if (target->slots[i].subtype)
      return -ERR_STATE;
    generations[i] = target->slots[i].generation;
  }
  if (hal_user_fork(&candidate, parent, (u64) target->owner, 0))
    return -ERR_INVALID;
  error = convert(spacesnapshotbegin(source->space, target->space));
  if (error)
    return error;
  for (i = 0; i < source->count; i++) {
    slot = &source->slots[i];
    if (slot->subtype != LOCAL_MEM || !(slot->inherit & LOCAL_FORK))
      continue;
    original = memory(slot);
    for (j = 0; j < count; j++)
      if (same(original, bindings[j].source))
        break;
    if (j != count)
      continue;
    if (memstat(original, &backing) || backing.kind == MEM_MMIO || slot->source == LOCAL_LOAD) {
      error = -ERR_DENIED;
      goto failed;
    }
    bindings[count].source = original;
    owned[count] = 0;
    adopted[count] = 0;
    error = spacesnapshotview(source->space, target->space, original, &bindings[count].target);
    if (error) {
      if (backing.state == MEM_DATA)
        error = spacesnapshotclone(source->space, target->space, original, &bindings[count].target,
                                   &adopted[count]);
      else if (backing.state == MEM_SEALED)
        error = memimmutable(original, target->acct, &bindings[count].target);
      else if (backing.state == MEM_SHARED) {
        bindings[count].target = original;
        error = memsharedhold(original);
      } else
        error = MEM_BAD;
      if (error) {
        error = convert(error);
        goto failed;
      }
      owned[count] = 1;
    }
    count++;
  }
  error = localfork(source, target, bindings, count, limits);
  if (error)
    goto failed;
  inherited = 1;
  error = localreserve(target, targetimage, reserve);
  if (error)
    goto failed;
  /* Last fallible stage: it may write child COW cookies but never parent data.
     Success owns no parent Rq/wait; the remaining commits cannot allocate. */
  error = sigfork(sourceimage, source->space, targetimage, target->space);
  if (error)
    goto failed;
  for (i = 0; i < count; i++)
    if (owned[i] && memfree(bindings[i].target))
      panic("fork temporary view commit");
  if (spacesnapshotend(source->space, target->space, 1))
    panic("fork mapping commit");
  for (i = 0; i < sizeof(Hctx); i++)
    ((u8 *) child)[i] = ((const u8 *) &candidate)[i];
  return 0;
failed:
  if (inherited) {
    if (localreserve(target, targetimage, 0))
      panic("fork floor rollback");
    for (i = 0; i < target->count; i++) {
      slot = &target->slots[i];
      if (slot->subtype && localrevoke(target, handlepack(HANDLE_RESOURCE, slot->generation, i)))
        panic("fork Local rollback");
      slot->generation = generations[i];
    }
  }
  for (i = 0; i < count; i++)
    if (owned[i] && memfree(bindings[i].target))
      panic("fork temporary view rollback");
  for (i = 0; i < count; i++)
    if (adopted[i] && memcloneundo(bindings[i].source, adopted[i]))
      panic("fork unmapped adoption rollback");
  if (spacesnapshotend(source->space, target->space, 0))
    panic("fork mapping rollback");
  return error;
}
