#ifndef _LIBSYS_EXECMANIFEST_H
#define _LIBSYS_EXECMANIFEST_H
#include "filewatch.h"
#include "conn.h"
#include <abi/boot.h>
#define SYSEXEC_PENDING 1
#define SYSEXEC_ACTIVE  2
#define SYSEXEC_ABORTED 3
#define SYSEXEC_ENDED   4

typedef struct Execmanifest Execmanifest;
typedef struct Execgroup    Execgroup;
typedef struct Exechold     Exechold;
typedef struct Execreserve  Execreserve;
typedef isize (*Execsource)(void *context, Handle pid, u64 image, u64 session, u64 reference);
typedef isize (*Exectake)(void *context, const Fileowner *source, const Fileowner *target,
                          const Execgroup *group, u64 *hold);
typedef isize (*Execdrop)(void *context, u64 hold);
struct Execgroup
{
  u64 session, reference;
  u32 retained, closed;
};
struct Execmanifest
{
  Fileinherit rows[BOOT_FD_COUNT];
  Execgroup   groups[BOOT_FD_COUNT];
  Handle      pid;
  u64         source_image, target_image;
  Handle      request;
  u64         connid, seq, slot, deadline;
  u32         count;
};
struct Exechold
{
  Execgroup group;
  u64       hold;
};
struct Execreserve
{
  Exechold entries[BOOT_FD_COUNT];
  u64      source, target;
  Handle   request;
  u64      connid, seq, slot, deadline, token;
  u32      count, ready;
};

/* Private serialized receiver. The admitted descriptor and data page are
   from the same original REQUEST. Supply disjoint private scratch and a
   read-only lookup of the exact source's live reference. Success publishes
   one group per source reference; retained=0 means CLOEXEC-only. This does
   not authorize the target Image or acquire/allocate a target reference. */
isize sysexecmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call,
                      const Fileslot *slot, const void *page, Connpeer backend, u64 now,
                      Execsource lookup, void *context);
/* Acquire the target observation through the original accepted REQUEST. The
   dispatcher owns and shares one Fileowner per target image; use the normal
   owner read/close path to retire the cookie after dependent work ends. */
isize sysexecobserve(Fileowner *owner, Handle permit, const Ipcmsg *call, u64 target);
/* Use paired native observations to decide the fate of the prepared
   references. PENDING is never permission to release them, even after their
   deadline. ABORTED permits target-only rollback. ACTIVE and ENDED require
   proof the source was replaced; only these permit exec-specific retirement
   of the old references. The dispatcher owns all actual cleanup. */
isize sysexecstate(Fileowner *source, Fileowner *target);
/* The serialized backend supplies persistent, initially zero storage. Take
   owns exactly one independent target hold on success and no hold on error;
   drop releases that exact hold and is retryable on failure. Hold IDs are
   private to the backend, not target reference_id or wire tokens. */
isize sysexecreserve(Execreserve *out, const Execmanifest *plan, Fileowner *source,
                     Fileowner *target, Exectake take, Execdrop drop, void *context);
/* Retry partial reservation rollback, or discard verified ended targets.
   Does not touch source File references or release observation cookies. */
isize sysexecdiscard(Execreserve *record, Fileowner *source, Fileowner *target, Execdrop drop,
                     void *context);
/* Caller has prepaid connection and token storage. Preserve the record and
   slot on failure. On success only the original request is finished; the
   token and its holds stay owned by the backend. */
isize sysexecreply(Execreserve *record, Filewatch *watch, Fileowner *target, const Ipcmsg *call,
                   u64 token, u64 now);
#endif
