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
typedef isize (*Execactivate)(void *context, const Fileowner *target, u64 hold);
typedef isize (*Execretire)(void *context, const Fileowner *source, const Execgroup *group);
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
  Handle      child;
  u64         snapshot;
  u32         nrows, captured;
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
  u32      count, ready, activated, retired;
};

/* Private serialized receiver. The admitted descriptor and data page are
   from the same original REQUEST. Supply disjoint private scratch and a
   read-only lookup of the exact source's live reference. Success publishes
   one group per source reference; retained=0 means CLOEXEC-only. This does
   not authorize the target Image or acquire/allocate a target reference. */
isize sysexecmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call,
                      const Fileslot *slot, const void *page, Connpeer backend, u64 now,
                      Execsource lookup, void *context);
/* Validate and group FORK_PREPARE rows. Fork retains every descriptor;
   child and snapshot are untrusted until checked against the live request
   and the original snapshot record. */
isize sysforkmanifest(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call,
                      const Fileslot *slot, const void *page, Connpeer backend, u64 now,
                      Execsource lookup, void *context);
/* Capture the original FORK_SNAPSHOT into private persistent storage. The
   owner supplies a fresh token from its own allocator. No holds are taken. */
isize sysforksnapshot(Execmanifest *out, const Filewatch *watch, const Ipcmsg *call,
                      const Fileslot *slot, const void *page, Connpeer backend, u64 now,
                      Execsource lookup, void *context, u64 token);
/* Read-only preflight. A successful commit invalidates the original record
   (clear count) after target holds and their cleanup have been recorded. */
isize sysforksnapshotcheck(const Execmanifest *snapshot, const Execmanifest *prepare, u64 now);
/* Decode the admitted PREPARE against the backend's still-owned snapshot.
   The original refs may be closed; only the captured groups and ordered
   rows are accepted. Failure leaves no plan and does not consume the
   snapshot. The caller must still verify the target with OBS_CHILD. */
isize sysforkprepare(Execmanifest *out, const Execmanifest *snapshot, const Filewatch *watch,
                     const Ipcmsg *call, const Fileslot *slot, const void *page, Connpeer backend,
                     u64 now);
/* Acquire the target observation through the original accepted REQUEST. The
   dispatcher owns and shares one Fileowner per target image; use the normal
   owner read/close path to retire the cookie after dependent work ends. */
isize sysexecobserve(Fileowner *owner, Handle permit, const Ipcmsg *call, u64 target);
/* FORK_PREPARE: the original REQUEST proves the caller owns preparation of
   this exact child image. No local state is installed on a rejected target. */
isize sysforkobserve(Fileowner *owner, Handle permit, const Ipcmsg *call, Handle child, u64 image);
/* Read only the exact OBS_CHILD target. The parent's state does not decide
   the child's fate. SYSEXEC_PENDING retains all holds, and only ABORTED or
   ENDED sets reason for acknowledgement after cleanup. */
isize sysforktarget(Fileowner *child);
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
/* After verified publication, activate target holds (which remain owned by
   the record) before retiring each old source reference. An already ended
   target needs no further activation. Zero commits one step; on error the
   same step is retried. Both observations remain owned by the dispatcher. */
isize sysexecadvance(Execreserve *record, Fileowner *source, Fileowner *target,
                     Execactivate activate, Execretire retire, void *context);
/* Retry partial reservation rollback, or discard verified ended targets.
   A published target requires source retirement first. */
isize sysexecdiscard(Execreserve *record, Fileowner *source, Fileowner *target, Execdrop drop,
                     void *context);
/* Caller has prepaid connection and token storage. Preserve the record and
   slot on failure. On success only the original request is finished; the
   token and its holds stay owned by the backend. */
isize sysexecreply(Execreserve *record, Filewatch *watch, Filelink *link, const Ipcmsg *call,
                   u64 token, u64 now);
/* The serialized backend supplies the exact target's prepaid, initialized
   connection after sysexecadvance completes. Its watch owns the observation
   used to reserve the token.
   A successful reply consumes the token and marks the connection delivered;
   the backend still owns the target holds and source retirement. */
isize sysexecclaim(Execreserve *record, Fileowner *source, Filelink *link, const Ipcmsg *call);
#endif
