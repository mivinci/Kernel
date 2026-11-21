#ifndef _LIBSYS_EXECMANIFEST_H
#define _LIBSYS_EXECMANIFEST_H
#include "filewatch.h"
#include "conn.h"
#include <abi/boot.h>

typedef struct Execmanifest Execmanifest;
typedef struct Execgroup    Execgroup;
typedef isize (*Execsource)(void *context, Handle pid, u64 image, u64 session, u64 reference);
struct Execgroup
{
  u64 session, reference;
  u32 retained, closed;
};
struct Execmanifest
{
  Fileinherit rows[BOOT_FD_COUNT];
  Execgroup   groups[BOOT_FD_COUNT];
  u32         count;
};

/* Private serialized receiver. The admitted descriptor and data page are
   from the same original REQUEST. Supply disjoint private scratch and a
   read-only lookup of the exact source's live reference. Success publishes
   one group per source reference; retained=0 means CLOEXEC-only. This does
   not authorize the target Image or acquire/allocate a target reference. */
isize sysexecmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call,
                      const Fileslot *slot, const void *page, Connpeer backend, u64 now,
                      Execsource lookup, void *context);
#endif
