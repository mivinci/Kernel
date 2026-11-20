#ifndef _LIBSYS_ARGUMENT_H
#define _LIBSYS_ARGUMENT_H
#include <abi/boot.h>
typedef struct Argsnap Argsnap;
struct Argsnap
{
  const u8 *data;
  u64       bytes;
  u32       argc, envc, argv_off, env_off;
};
STATIC_ASSERT(sizeof(Argsnap) == 32, argument_snapshot_view);
/* Caller owns private prepaid destination and excludes caught signals for
   the bounded capture. Sources/destination/outputs are separate. Failure may
   dirty scratch but never publishes out. String lengths include the NUL.
   No allocation, original pointers, locale or UTF-8 policy. */
isize sysargstring(void *data, u64 capacity, const char *source, u64 *bytes);
/* Counts each vector through its NULL, at most BOOT_ARG_COUNT entries each.
   Private Strref tables, strings and two zero pointer-budget words together
   <=BOOT_ARG_BYTES. NULL vector means empty. Caller gives storage; path is
   captured separately with its independent limit, not charged to argv/env. */
isize sysargcopy(void *data, u64 capacity, const char *const *argv, const char *const *envp,
                 Argsnap *out);
/* Borrow a checked accepted string; no original pointer table is consulted. */
isize sysargat(const Argsnap *snapshot, u32 environment, u64 index, const char **text, u64 *bytes);
#endif
