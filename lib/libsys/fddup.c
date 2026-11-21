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

static int
disjoint(const void *base, u64 size, const u64 *closing)
{
  u64 start = (u64) (uintptr_t) base, out = (u64) (uintptr_t) closing;
  return base && closing && size <= UINT64_MAX - start && sizeof(*closing) <= UINT64_MAX - out &&
         (out + sizeof(*closing) <= start || start + size <= out);
}

isize
sysfddup2(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 target, u64 *closing)
{
  Fdview  from, to, removed;
  Fileref reference;
  u64     last = 0;
  isize   error;
  if (!disjoint(table, sizeof(*table), closing) || !disjoint(pool, sizeof(*pool), closing))
    return -ERR_INVALID;
  error = sourcefile(table, pool, self, source, &from);
  if (error)
    return error;
  if (source == target) {
    *closing = 0;
    return target;
  }
  error = sysfdobserve(table, target, &to);
  if (error)
    return error;
  if (to.tag.state == SYSFD_LIVE) {
    error = sysfilerefview(pool, self, to.file, &reference);
    if (error)
      return error;
    if (reference.state != SYSFILE_REF_LIVE || !reference.aliases)
      return -ERR_STATE;
    if (to.file != from.file && reference.aliases == 1)
      last = to.file;
  }
  error = sysfilerefalias(pool, self, from.file);
  if (error)
    return error;
  error = sysfdreplace(table, to.tag, from.file, 0, &removed);
  if (error) {
    (void) sysfilerefdrop(pool, self, from.file);
    return error;
  }
  /* Validated before exchange and protected from signals throughout. The
     last alias leaves its stable File slot in CLOSING, even with live pins. */
  if (removed.tag.state == SYSFD_LIVE)
    (void) sysfilerefdrop(pool, self, removed.file);
  *closing = last;
  return target;
}
