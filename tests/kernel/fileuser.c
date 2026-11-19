#include "fileuser.h"
#include "private/file.h"
#include <unistd.h>
static u64       area[FS_CONN_BYTES / sizeof(u64)];
static Filepages pages;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(221);                                                                                  \
  } while (0)
u32
fileusercheck(void)
{
  Filehead *head = (Filehead *) area, copied;
  Fileslot *slot = (Fileslot *) ((u8 *) area + FS_DESC_OFFSET), descriptor;
  Filelease active[6], extra, stale;
  u32       i;
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
