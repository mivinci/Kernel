#include "fileuser.h"
#include "private/file.h"
#include "private/filewatch.h"
#include <unistd.h>
static u64       area[FS_CONN_BYTES / sizeof(u64)];
static Filepages pages;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(221);                                                                                  \
  } while (0)
static void
openpoolcheck(void)
{
  Fileowner       owner = {1, 1, 1, 0};
  const Openlimit limit = {1, 1};
  Openprep        pool[1] = {{0}};
  Openprep       *prep;
  Handle          client = (Handle) (UINT64_C(1) << 59 | UINT64_C(1) << 16 | 2);
  Ipcmsg          call = {0};
  u64             old, current = 0;

  CHECK(!sysopenreserve(pool, 1, limit, &owner, client, 1, 7, 8, 10, &old));
  prep = sysopenlookup(pool, 1, old);
  CHECK(prep && sysopenrelease(pool, 1, old) == -ERR_BUSY &&
        sysopenreserve(pool, 1, limit, &owner, client, 1, 9, 10, 20, &current) == -ERR_CAPACITY &&
        !current);
  call.kind = IPC_CALL;
  call.source = client;
  call.image = 1;
  call.words[0] = FS_ABORT;
  call.words[2] = 1;
  call.words[3] = old;
  CHECK(sysopenabort(prep, &call) == 1);
  sysopenretire(prep);
  CHECK(!sysopenabort(prep, &call) && !sysopenrelease(pool, 1, old));
  CHECK(!sysopenreserve(pool, 1, limit, &owner, client, 1, 9, 10, 20, &current) && current > old &&
        !sysopenlookup(pool, 1, old) && sysopenrelease(pool, 1, old) == -ERR_STALE);
  prep = sysopenlookup(pool, 1, current);
  CHECK(prep && prep->reference == 10 && sysopenabort(prep, &call) == -ERR_STALE);
  call.words[0] = FS_ATTACH;
  call.words[3] = current;
  CHECK(!sysopenattach(prep, &call, 19) && sysopenattach(prep, &call, 20) == -ERR_STALE);
  sysopenretire(prep);
  CHECK(!sysopenrelease(pool, 1, current));
  pool[0].token = UINT64_MAX & ~UINT64_C(65535);
  CHECK(sysopenreserve(pool, 1, limit, &owner, client, 1, 9, 10, 20, &current) == -ERR_CAPACITY);
}
static void
openlimitcheck(void)
{
  Fileowner       vfs = {1, 1, 1, 0}, newer = {1, 2, 2, 0};
  const Openlimit limit = {1, 1};
  Openprep        pool[3] = {{0}};
  Handle          client = (Handle) (UINT64_C(1) << 59 | UINT64_C(1) << 16 | 2);
  u64             first, second, untouched = 0;
  CHECK(!sysopenreserve(pool, 3, limit, &vfs, client, 1, 7, 8, 10, &first));
  CHECK(sysopenreserve(pool, 3, limit, &vfs, client, 2, 9, 10, 20, &untouched) == -ERR_CAPACITY);
  CHECK(sysopenreserve(pool, 3, limit, &newer, client, 1, 9, 10, 20, &untouched) == -ERR_CAPACITY &&
        !untouched);
  CHECK(!sysopenreserve(pool, 3, limit, &newer, client, 2, 9, 10, 20, &second));
  sysopenretire(sysopenlookup(pool, 3, second));
  CHECK(sysopenreserve(pool, 3, limit, &newer, client, 3, 11, 12, 30, &untouched) == -ERR_CAPACITY);
  CHECK(!sysopenrelease(pool, 3, second));
  sysopencommit(sysopenlookup(pool, 3, first));
  CHECK(!sysopenreserve(pool, 3, limit, &vfs, client, 2, 9, 10, 20, &second));
  CHECK(sysopenreserve(pool, 3, limit, &newer, client, 1, 11, 12, 30, &untouched) == -ERR_CAPACITY);
}
static isize
sweeprevoke(void *context, const Openprep *prep)
{
  int *fail = context;
  return *fail && prep->reference == 2 ? -ERR_BUSY : 0;
}
static void
opensweepcheck(void)
{
  Fileowner       owner = {1, 1, 1, 0};
  const Openlimit limit = {3, 3};
  Openprep        pool[3] = {{0}};
  Handle          client = (Handle) (UINT64_C(1) << 59 | UINT64_C(1) << 16 | 2);
  u64             one, two, three;
  u32             retired;
  int             fail = 1;
  CHECK(!sysopenreserve(pool, 3, limit, &owner, client, 1, 1, 1, 8, &one) &&
        !sysopenreserve(pool, 3, limit, &owner, client, 1, 2, 2, 9, &two) &&
        !sysopenreserve(pool, 3, limit, &owner, client, 1, 3, 3, 7, &three));
  sysopencommit(sysopenlookup(pool, 3, three));
  CHECK(sysopennext(pool, 3) == 8 &&
        sysopensweep(pool, 3, 9, NULL, sweeprevoke, &fail, &retired) == -ERR_BUSY && retired == 1 &&
        sysopenlookup(pool, 3, one)->state == Opencancelled && sysopennext(pool, 3) == 9);
  fail = 0;
  CHECK(!sysopensweep(pool, 3, 9, NULL, sweeprevoke, &fail, &retired) && retired == 1 &&
        sysopenlookup(pool, 3, two)->state == Opencancelled &&
        sysopenlookup(pool, 3, three)->state == Openattached && !sysopennext(pool, 3));
}
u32
fileusercheck(void)
{
  Filehead *head = (Filehead *) area, copied;
  Fileslot *slot = (Fileslot *) ((u8 *) area + FS_DESC_OFFSET), descriptor;
  Filelease active[6], extra, stale;
  u32       i;
  openpoolcheck();
  openlimitcheck();
  opensweepcheck();
  for (i = 0; i < ARRAY_SIZE(area); i++)
    area[i] = 0;
  head->magic = FS_CONN_MAGIC;
  head->bytes = FS_CONN_BYTES;
  head->connid = ((u64) 5 << 16) | 3;
  head->serverimage = 17;
  head->slots = FS_CONN_SLOTS;
  head->slotbytes = FS_SLOT_BYTES;
  CHECK(!sysfilehead(area, sizeof(area), head->connid, 17, &copied));
  head->connid++;
  CHECK(sysfilehead(area, sizeof(area), ((u64) 5 << 16) | 3, 17, &copied) == -ERR_STALE &&
        copied.connid == (((u64) 5 << 16) | 3));
  head->connid--;
  CHECK(sysfilehead(area, 4096, head->connid, 17, &copied) == -ERR_INVALID);
  slot[0].seq = 11;
  slot[0].state = FS_SLOT_SUBMITTED;
  slot[0].span = 2;
  slot[0].payload_bytes = 8192;
  slot[0].object = 99;
  slot[0].args[7] = 55;
  CHECK(!sysfilerequest(area, sizeof(area), 0, 11, &descriptor));
  slot[0].args[7] = 56;
  CHECK(descriptor.args[7] == 55 && descriptor.span == 2);
  slot[1].args[0] = 1;
  CHECK(sysfilerequest(area, sizeof(area), 0, 11, &descriptor) == -ERR_INVALID);
  slot[1].args[0] = 0;
  slot[0].state = FS_SLOT_DONE;
  CHECK(sysfilerequest(area, sizeof(area), 0, 11, &descriptor) == -ERR_INVALID);
  sysfilepagesinit(&pages);
  CHECK(!sysfilereserve(&pages, SYSFILE_MAIN, 1, &active[0]) && active[0].slot == 0 &&
        !sysfilereserve(&pages, SYSFILE_MAIN, 1, &active[1]) && active[1].slot == 1 &&
        sysfilereserve(&pages, SYSFILE_MAIN, 1, &extra) == -ERR_BUSY &&
        !sysfilereserve(&pages, SYSFILE_SIGNAL1, 2, &active[2]) && active[2].slot == 2 &&
        !sysfilereserve(&pages, SYSFILE_SIGNAL2, 2, &active[3]) && active[3].slot == 4 &&
        !sysfilereserve(&pages, SYSFILE_CONTROL, 1, &active[4]) && active[4].slot == 6 &&
        !sysfilereserve(&pages, SYSFILE_CLEANUP, 1, &active[5]) && active[5].slot == 7);
  stale = active[2];
  extra.seq = stale.seq;
  extra.slot = 3;
  extra.span = 1;
  CHECK(sysfilerelease(&pages, extra) == -ERR_STALE && !sysfilerelease(&pages, stale) &&
        !sysfilereserve(&pages, SYSFILE_SIGNAL1, 2, &active[2]) && active[2].seq > stale.seq &&
        sysfilerelease(&pages, stale) == -ERR_STALE && !sysfilepagescheck(&pages));
  /* A writable shared DONE word never changes private occupancy. */
  CHECK(slot[0].state == FS_SLOT_DONE && pages.pages[0].seq == active[0].seq &&
        sysfilereserve(&pages, SYSFILE_MAIN, 1, &extra) == -ERR_BUSY);
  for (i = 0; i < ARRAY_SIZE(active); i++)
    CHECK(!sysfilerelease(&pages, active[i]));
  pages.sequence = UINT64_MAX - 1;
  CHECK(!sysfilereserve(&pages, SYSFILE_MAIN, 2, &extra) && extra.seq == UINT64_MAX &&
        !sysfilerelease(&pages, extra) &&
        sysfilereserve(&pages, SYSFILE_CONTROL, 1, &extra) == -ERR_OVERFLOW &&
        !sysfilepagescheck(&pages));
  return 1;
}
