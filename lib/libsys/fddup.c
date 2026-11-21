#include "private/fd.h"

static isize
sourcefile(Fdtable *table, Filepool *pool, Connpeer self, u32 source, Fdview *old)
{
  Fileref reference;
  isize   error = sysfdobserve(table, source, old);
  if (error)
    return error;
  if (old->tag.state != SYSFD_LIVE)
    return -ERR_BAD_HANDLE;
  error = sysfilerefview(pool, self, old->file, &reference);
  if (error)
    return error;
  return reference.state == SYSFILE_REF_LIVE && reference.aliases ? 0 : -ERR_STATE;
}

isize
sysfddup(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 minimum)
{
  Fdview old;
  Fdtag  slot;
  isize  error;
  if (minimum >= BOOT_FD_COUNT)
    return -ERR_INVALID;
  error = sourcefile(table, pool, self, source, &old);
  if (error)
    return error;
  error = sysfilerefalias(pool, self, old.file);
  if (error)
    return error;
  error = sysfdreserve(table, minimum, &slot);
  if (!error) {
    error = sysfdpublish(table, slot, old.file, 0);
    if (!error)
      return slot.index;
    /* The caller keeps both tables stable, so these rollback operations
       still own the same slot and a nonfinal alias. */
    (void) sysfdcancel(table, slot);
  }
  (void) sysfilerefdrop(pool, self, old.file);
  return error;
}

isize
sysfddup2(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 target, Fdview *removed)
{
  Fdview from, to;
  u64    base = (u64) (uintptr_t) pool, out = (u64) (uintptr_t) removed;
  isize  error;
  /* sysfdreplace separately rejects an output overlapping the fd table. */
  if (!removed || sizeof(*pool) > UINT64_MAX - base || sizeof(*removed) > UINT64_MAX - out ||
      (out < base + sizeof(*pool) && base < out + sizeof(*removed)))
    return -ERR_INVALID;
  error = sourcefile(table, pool, self, source, &from);
  if (error || source == target)
    return error ? error : (isize) target;
  error = sysfdobserve(table, target, &to);
  if (error)
    return error;
  error = sysfilerefalias(pool, self, from.file);
  if (error)
    return error;
  error = sysfdreplace(table, to.tag, from.file, 0, removed);
  if (error) {
    (void) sysfilerefdrop(pool, self, from.file);
    return error;
  }
  return target;
}
