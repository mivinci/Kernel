#ifndef _LIBC_SPAWNFILES_H
#define _LIBC_SPAWNFILES_H
#include <spawn.h>
#include "private/fd.h"

#define SPAWNFILES_LIMIT 128

typedef struct Spawnaction Spawnaction;
enum
{
  SPAWNFILE_OPEN = 1,
  SPAWNFILE_CLOSE,
  SPAWNFILE_DUP2,
};
struct Spawnaction
{
  Spawnaction *next;
  const char  *path;
  int          kind, fd, target, flags;
  mode_t       mode;
};
/* Execute against the caller's unpublished child state, never the parent fd
   table. The caller must keep this list immutable through replay. Stop on the
   first positive errno; the caller cancels the child preparation on error. */
int spawnfilesapply(const posix_spawn_file_actions_t *, int (*)(void *, const Spawnaction *),
                    void *);
/* The table and pool belong exclusively to the unpublished child. openfile
   uses its credentials/cwd/umask and returns one owned alias on success.
   Last-alias drops remain in the pool's prepaid close records. Failure
   leaves the private state with the caller for preparation cleanup. */
int spawnfilesprepare(const posix_spawn_file_actions_t *, Fdtable *, Filepool *, Connpeer,
                      int (*openfile)(void *, const char *, int, mode_t, u64 *), void *);
#endif
