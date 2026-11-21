#include <fork.h>
#include <prep.h>
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
static Memid
memory(const Local *slot)
{
  Memid id;
  id.generation = slot->objectgen;
  id.index = slot->object;
  id.reserved = 0;
  return id;
}

static int
memcheckpoint(Forkcheck checkpoint, Prepid prep, Rtable *source, Rtable *target)
{
  int error = checkpoint(prep);
  return error ? error : prepsnapshotcheck(prep, source, target);
}

int
forksnapshotchecked(Rtable *source, Rtable *target, const Createarg *limits, u32 reserve,
                    const Hctx *parent, Hctx *child, Prepid prep, Forkcheck checkpoint)
{
  Hctx    candidate;
  Imageid sourceimage, targetimage;
  Image   image;
  Life    life;
  Mem     backing;
  Memid   original, view;
  Local  *slot;
  u32     i, owned;
  int     error;
  if (!source || !target || source == target || !source->slots || !target->slots ||
      source->state != LOCAL_OPEN || target->state != LOCAL_OPEN || !source->count ||
      source->count > LOCALS || source->count != target->count || !limits || reserve > 8 ||
      prep.reserved || (!prep.generation && (prep.index || checkpoint)) || !parent || !child ||
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
  }
  if (hal_user_fork(&candidate, parent, (u64) target->owner, 0))
    return -ERR_INVALID;
  if (prep.generation) {
    error = prepsnapshotcheck(prep, source, target);
    if (error)
      return error;
  }
  error = convert(spacesnapshotopen(source->space, target->space, prep));
  if (error)
    return error;
  for (;;) {
    error = spacesnapshotbuild(source->space, target->space);
    if (error <= 0)
      break;
    if (checkpoint && (error = checkpoint(prep)))
      goto failed;
  }
  if (error) {
    error = convert(error);
    goto failed;
  }
  for (i = 0; i < source->count; i++) {
    if (checkpoint && i && i % 8 == 0) {
      error = memcheckpoint(checkpoint, prep, source, target);
      if (error)
        goto failed;
    }
    slot = &source->slots[i];
    if (slot->subtype != LOCAL_MEM || !(slot->inherit & LOCAL_FORK))
      continue;
    original = memory(slot);
    if (!localforkview(target, original, &view))
      continue;
    if (memstat(original, &backing) || backing.kind == MEM_MMIO || slot->source == LOCAL_LOAD) {
      error = -ERR_DENIED;
      goto failed;
    }
    owned = 0;
    error = spacesnapshotview(source->space, target->space, original, &view);
    if (error) {
      if (backing.state == MEM_DATA) {
        error = spacesnapshotclone(source->space, target->space, original, &view);
        owned = !error;
      } else if (backing.state == MEM_SEALED) {
        error = memimmutable(original, target->acct, &view);
        owned = !error;
      } else if (backing.state == MEM_SHARED) {
        view = original;
        error = 0;
      } else
        error = MEM_BAD;
      if (error) {
        error = convert(error);
        goto failed;
      }
    }
    /* A fresh leaf reference crosses no checkpoint: bind acquires the durable
       target-table manager, then this local creation reference is returned. */
    error = localforkbind(source, target, i, view);
    if (owned && memfree(view))
      panic("fork leaf owner transfer");
    if (error)
      goto failed;
  }
  if (checkpoint) {
    error = memcheckpoint(checkpoint, prep, source, target);
    if (error)
      goto failed;
  }
  error = localforkload(source, target, limits);
  if (error)
    goto failed;
  error = localforkreserve(source, target, targetimage, reserve);
  if (error)
    goto failed;
  /* Last fallible stage: it may write child COW cookies but never parent data.
     Success owns no parent Rq/wait; the remaining commits cannot allocate. */
  error = sigforksnapshot(sourceimage, source->space, targetimage, target->space);
  if (error)
    goto failed;
  for (i = 0; i < target->count; i++) {
    slot = &target->slots[i];
    if (slot->subtype == LOCAL_MEM && slot->source & LOCAL_STAGED && memforkcommit(memory(slot)))
      panic("fork view journal commit");
  }
  if (localforkfinish(source, target))
    panic("fork local lineage commit");
  if (spacesnapshotend(source->space, target->space, 1))
    panic("fork mapping commit");
  for (i = 0; i < sizeof(Hctx); i++)
    ((u8 *) child)[i] = ((const u8 *) &candidate)[i];
  return 0;
failed:
  if (localforkdrop(target))
    panic("fork staged ownership rollback");
  if (spacesnapshotend(source->space, target->space, 0))
    panic("fork mapping rollback");
  return error;
}

int
forksnapshot(Rtable *source, Rtable *target, const Createarg *limits, u32 reserve,
             const Hctx *parent, Hctx *child, Prepid prep)
{
  return forksnapshotchecked(source, target, limits, reserve, parent, child, prep, NULL);
}
