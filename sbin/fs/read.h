#ifndef _FS_READ_H
#define _FS_READ_H
#include "cache.h"
#include "load.h"
#include "seek.h"

/* The serialized dispatcher has authorized a readable regular File and
   admitted this request. Map/index describe its current inode version.
   On WOULD_BLOCK no bytes or cursor are changed; the caller retains the
   request and may stage missing blocks before retrying. Scratch is private,
   independent storage of CACHE_BYTES; page is the admitted data page. */
isize fileread(Fileinflight *active, const Ipcmsg *call, const Fileslot *slot, const Loadmap *map,
               const Loadindex *index, Cache *cache, u32 first, u64 *position, void *scratch,
               volatile u8 *page);
#endif
