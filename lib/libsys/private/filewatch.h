#ifndef _LIBSYS_FILEWATCH_H
#define _LIBSYS_FILEWATCH_H
#include "file.h"
#include <sys/channel.h>
#define SYSFILE_OPEN_TTL_NS UINT64_C(10000000000)
#define SYSOPEN_POOL_MAX    256

typedef struct Fileowner Fileowner;
typedef struct Filewatch Filewatch;
typedef struct Filelink  Filelink;
typedef struct Openprep  Openprep;
typedef struct Openlimit Openlimit;
typedef isize (*Openrevoke)(void *context, const Openprep *prep);
typedef isize (*Filedrain)(void *context, Filelink *link);
enum
{
  Openempty,
  Openpending,
  Openattached,
  Openclosed,
  Opencancelled
};
struct Openprep
{
  Handle preparer, client;
  u64    preparer_image, client_image, token, session, reference, deadline;
  u32    state;
};
struct Openlimit
{
  u32 client, preparer;
};

/* Start with a zeroed service-owned array; its address and count stay fixed.
   Keep the output token separate from the array.
   Once a token is published, keep the tombstone for valid retries and release
   it only when those dependencies and all cleanup have been discharged. */
/* Client quota covers all of its records; preparer quota covers pending
   records and cancellations before transfer, including retained tombstones. */
isize     sysopenreserve(Openprep *pool, u32 count, Openlimit limit, const Fileowner *owner,
                         Handle client, u64 image, u64 session, u64 reference, u64 deadline,
                         u64 *token);
Openprep *sysopenlookup(Openprep *pool, u32 count, u64 token);
isize     sysopenrelease(Openprep *pool, u32 count, u64 token);
/* Serialize the check, resource reply and commit in the service loop. */
isize sysopenattach(const Openprep *prep, const Ipcmsg *call, u64 now);
void  sysopencommit(Openprep *prep);
/* Confirm ATTACH over an already delivered connection without sending MEM.
   On zero the REQUEST was settled; cancellation/drain does not commit prep. */
isize sysopenreuse(Openprep *prep, const Filelink *link, const Ipcmsg *call, u64 now);
/* Resolve only a live attached reference after connection and slot validation.
   The caller still owns logical close and any downstream drain. */
Openprep *sysopenref(Openprep *pool, u32 count, const Ipcmsg *call, const Fileslot *slot);
/* Resolve a trusted client identity and exact reference (e.g. an admitted
   CLOSE or a published exec manifest). Before returning success, revoke must
   close the reference or save its downstream cleanup in preallocated storage.
   On failure the entry remains attached. A closed entry is idempotent. */
isize sysopenclose(Openprep *pool, u32 count, Handle client, u64 image, u64 session, u64 reference,
                   Openrevoke revoke, void *context);
/* Validate any nonzero connid against the caller's connection first. Return
   1 while a logical close is owed, or 0 for a tombstone. Retire only after
   closing the reference or saving its cleanup. */
isize sysopenabort(const Openprep *prep, const Ipcmsg *call);
/* Settle the original ABORT. A nonzero connid must match the supplied ready
   link and its caller. A revoke error preserves the reference and REQUEST. */
isize sysopenabortreply(Openprep *pool, u32 count, const Filelink *link, const Ipcmsg *call,
                        Openrevoke revoke, void *context);
int   sysopenexpired(const Openprep *prep, u64 now);
int   sysopenorphan(const Openprep *prep, const Fileowner *owner);
void  sysopenretire(Openprep *prep);
/* Event loop helpers. Use the earliest nonzero deadline when arming the
   shared service alarm. Sweep before dispatch and after a LIFE/TIMER hint.
   Revoke must close the reference or record its cleanup before returning 0;
   a failure leaves that entry pending and preserves earlier progress. */
u64   sysopennext(const Openprep *pool, u32 count);
isize sysopensweep(Openprep *pool, u32 count, u64 now, const Fileowner *ended, Openrevoke revoke,
                   void *context, u32 *retired);
struct Fileowner
{
  Handle pid;
  u64    image, cookie, reason;
};
struct Filewatch
{
  Fileowner    owner;
  u64          connid;
  Fileinflight active;
};

/* Share one zero-initialized owner record among all dependencies on the same
   source image. Clear it only once those dependencies are retired or moved. */
isize sysfileowner(Fileowner *owner, Handle permit, const Ipcmsg *source);
isize sysfileownerread(Fileowner *owner);
isize sysfileownerclose(Fileowner *owner);
/* Monotonic nanoseconds for ordinary OPEN preparation. A zero deadline
   means no pending preparation; clear it only after ATTACH commits or the
   reference is revoked. Inheritance has a separate lifetime rule. */
isize sysfilenow(u64 *now);
isize sysfileopendeadline(u64 *deadline);

/* Use a zero-initialized watch in private storage separate from the region
   and descriptor output. The serialized service loop supplies a received
   CALL and a reserved unique connection ID before publishing the connection. */
isize sysfilewatch(Filewatch *watch, Handle permit, const Ipcmsg *source, u64 connid);
/* Refresh on LIFE hints. ENDED closes admission but leaves the original
   cookie and all in-flight ownership in place. */
isize sysfilewatchread(Filewatch *watch);
isize sysfilewatchaccept(Filewatch *watch, const volatile void *region, u64 bytes,
                         const Ipcmsg *source, Fileslot *out);
/* Caller has stopped every downstream access for this request. Finish the
   original retired REQUEST and only then release its registered slots. */
isize sysfilewatchdrain(Filewatch *watch, Handle request);
/* Only after retiring the connection's business references and stopping
   downstream access. A busy ledger prevents both ACK and DROP. */
isize sysfilewatchclose(Filewatch *watch);

struct Filelink
{
  Filewatch watch;
  Handle    memory, space;
  u64       address, serverimage, generation;
  u32       ready, prepaid;
};

/* First ATTACH: verify the prepared reference, observe the source, then
   deliver a new slot mapping. Only successful delivery commits the prep. */
isize sysopenfirst(Openprep *prep, Filelink *pool, u32 count, Handle permit, Handle allocator,
                   Handle space, u64 base, const Ipcmsg *call, u64 now);

/* Use a fixed zeroed pool and an exclusive VA arena in the service's own
   Space. Serialize requests and keep other observation dependencies separate
   until they can share ownership of the source cookie. */
isize sysfilehello(Filelink *pool, u32 count, Handle permit, Handle allocator, Handle space,
                   u64 base, const Ipcmsg *call);
/* Generic shared-slot handshake for services that use the common
   Protohead/Protoslot wire layout but have their own HELLO opcode. */
isize sysslothello(Filelink *pool, u32 count, Handle permit, Handle allocator, Handle space,
                   u64 base, u64 hello, const Ipcmsg *call);
/* Fund an observed target's connection without delivering its MEM. The
   dispatcher first installs the target observation at index. On failure it
   owns cleanup, including any incomplete mapping or observation. */
isize sysfileprepay(Filelink *pool, u32 count, u32 index, Handle allocator, Handle space, u64 base);
/* Route by full connid and source before reading the shared head or slot. */
isize sysfilelinkaccept(Filelink *pool, u32 count, const Ipcmsg *call, Fileslot *out);
/* Call only once all business references and downstream accesses are settled.
   A failed cleanup retains its remaining obligations and blocks admission. */
isize sysfilelinkclose(Filelink *link);
/* On a LIFE hint, refresh every live observation. Drain must settle business
   references and outstanding requests before returning success. Failed entries
   remain for retry; the scan still progresses on other entries. */
isize sysfilelinksweep(Filelink *pool, u32 count, Filedrain drain, void *context);
#endif
