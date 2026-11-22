#include "fdtableuser.h"
#include "private/fd.h"
#include <unistd.h>
static Fdtable table;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(220);                                                                                  \
  } while (0)
u32
fdtableusercheck(void)
{
  Fdtag  reserved, oldtag, other;
  Fdview value, moved, empty;
  Fdload startup[] = {{127, 700, BOOT_FD_CLOEXEC}, {1, 700, 0}};
  u32    i;
  sysfdinit(&table);
  CHECK(!sysfdcheck(&table) && !sysfdobserve(&table, 0, &empty) && empty.tag.state == SYSFD_FREE &&
        !empty.file);
  CHECK(!sysfdreserve(&table, 0, &reserved) && reserved.index == 0 &&
        sysfdreplace(&table, reserved, 11, 0, &moved) == -ERR_BUSY);
  oldtag = reserved;
  CHECK(!sysfdpublish(&table, reserved, 101, BOOT_FD_CLOEXEC) &&
        sysfdcancel(&table, oldtag) == -ERR_STALE && !sysfdobserve(&table, 0, &value) &&
        value.file == 101 && value.flags == BOOT_FD_CLOEXEC);
  oldtag = value.tag;
  CHECK(!sysfdreserve(&table, 0, &other) && other.index == 1 &&
        !sysfdpublish(&table, other, 101, BOOT_FD_CLOEXEC) && !sysfdsetflags(&table, oldtag, 0) &&
        !sysfdobserve(&table, 1, &value) && value.flags == BOOT_FD_CLOEXEC);
  CHECK(!sysfdreplace(&table, oldtag, 202, 0, &moved) && moved.file == 101 &&
        moved.tag.index == 0 && !moved.flags && sysfddetach(&table, oldtag, &value) == -ERR_STALE &&
        sysfdreplace(&table, empty.tag, 303, 0, &value) == -ERR_STALE);
  CHECK(!sysfdobserve(&table, 0, &value) && value.file == 202 &&
        !sysfddetach(&table, value.tag, &moved) && moved.file == 202 &&
        !sysfdobserve(&table, 1, &value) && value.file == 101 &&
        !sysfddetach(&table, value.tag, &moved) && moved.file == 101);
  for (i = 0; i < BOOT_FD_COUNT; i++) {
    CHECK(!sysfdreserve(&table, 0, &reserved) && reserved.index == i &&
          !sysfdpublish(&table, reserved, 1000 + i, i & 1));
  }
  CHECK(sysfdreserve(&table, 0, &reserved) == -ERR_CAPACITY);
  for (i = 0; i < BOOT_FD_COUNT; i++) {
    CHECK(!sysfdobserve(&table, i, &value) && value.file == 1000 + i &&
          !sysfddetach(&table, value.tag, &moved) && moved.file == 1000 + i &&
          moved.flags == (i & 1));
  }
  CHECK(!sysfdobserve(&table, 127, &empty) && !sysfdreserve(&table, 1, &reserved) &&
        reserved.index == 1 && sysfdload(&table, startup, ARRAY_SIZE(startup)) == -ERR_BUSY &&
        !sysfdobserve(&table, 127, &value) && value.tag.generation == empty.tag.generation &&
        !value.file && value.tag.state == SYSFD_FREE && !sysfdcancel(&table, reserved));
  startup[1].fd = 127;
  CHECK(sysfdload(&table, startup, ARRAY_SIZE(startup)) == -ERR_INVALID &&
        !sysfdobserve(&table, 127, &value) && value.tag.generation == empty.tag.generation &&
        !value.file);
  startup[1].fd = 1;
  CHECK(!sysfdload(&table, startup, ARRAY_SIZE(startup)) && !sysfdobserve(&table, 127, &value) &&
        value.tag.generation == empty.tag.generation + 1 && value.file == 700 &&
        value.flags == BOOT_FD_CLOEXEC && !sysfddetach(&table, value.tag, &moved) &&
        !sysfdobserve(&table, 1, &value) && value.file == 700 && !value.flags &&
        !sysfddetach(&table, value.tag, &moved) && !sysfdobserve(&table, 0, &value) &&
        value.tag.state == SYSFD_FREE);
  /* White-box exhaustion is bounded evidence; no billions-of-calls loop. */
  table.entries[BOOT_FD_COUNT - 1].generation = UINT64_MAX - 1;
  CHECK(!sysfdreserve(&table, BOOT_FD_COUNT - 1, &reserved) && reserved.generation == UINT64_MAX &&
        !sysfdpublish(&table, reserved, 404, 0) &&
        !sysfdobserve(&table, BOOT_FD_COUNT - 1, &value) &&
        sysfdreplace(&table, value.tag, 505, 0, &moved) == -ERR_OVERFLOW &&
        !sysfddetach(&table, value.tag, &moved) && moved.file == 404 &&
        sysfdreserve(&table, BOOT_FD_COUNT - 1, &reserved) == -ERR_CAPACITY && !sysfdcheck(&table));
  return 1;
}
