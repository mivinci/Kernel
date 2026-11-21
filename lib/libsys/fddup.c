#include "private/fd.h"

isize
sysfddup(Fdtable *table, Filepool *pool, Connpeer self, u32 source, u32 minimum)
{
  Fdview  old;
  Fdtag   slot;
  Fileref reference;
  isize   error;
  if (minimum >= BOOT_FD_COUNT)
    return -ERR_INVALID;
  error = sysfdobserve(table, source, &old);
  if (error)
    return error;
  if (old.tag.state != SYSFD_LIVE)
    return -ERR_BAD_HANDLE;
  error = sysfilerefview(pool, self, old.file, &reference);
  if (error)
    return error;
  if (reference.state != SYSFILE_REF_LIVE || !reference.aliases)
    return -ERR_STATE;
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
