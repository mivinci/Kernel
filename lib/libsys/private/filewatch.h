#ifndef _LIBSYS_FILEWATCH_H
#define _LIBSYS_FILEWATCH_H
#include "file.h"
#include <sys/channel.h>
#define SYSFILE_OPEN_TTL_NS UINT64_C(10000000000)
#define SYSOPEN_POOL_MAX    256

typedef struct Fileowner Fileowner;
typedef struct Filewatch Filewatch;
typedef struct Openprep  Openprep;
typedef struct Openlimit Openlimit;
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
/* Validate any nonzero connid against the caller's connection first. Return
   1 while a logical close is owed, or 0 for a tombstone. Retire only after
   closing the reference or saving its cleanup. */
isize sysopenabort(const Openprep *prep, const Ipcmsg *call);
int   sysopenexpired(const Openprep *prep, u64 now);
int   sysopenorphan(const Openprep *prep, const Fileowner *owner);
void  sysopenretire(Openprep *prep);
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
/* Only after retiring the connection's business references and stopping
   downstream access. A busy ledger prevents both ACK and DROP. */
isize sysfilewatchclose(Filewatch *watch);
#endif
