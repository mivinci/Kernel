#ifndef _FS_PATH_H
#define _FS_PATH_H
#include <types.h>

#define FS_PATH_BYTES 4096
#define FS_NAME_BYTES 255
#define FS_PATH_LINKS 40

typedef struct Fspath   Fspath;
typedef struct Pathpart Pathpart;
enum
{
  PATH_ROOT = 1,
  PATH_NAME,
  PATH_DOT,
  PATH_PARENT,
};
struct Fspath
{
  u32  bytes, cursor, links, expandable;
  char text[FS_PATH_BYTES];
};
struct Pathpart
{
  const char *name;
  u32         bytes, kind, final, directory;
};

/* Copy the input, including its NUL, before validating it. The link target
   must be private and must not alias the walk. Errors are negative errno. */
isize fspathinit(Fspath *walk, const volatile char *path, u64 bytes);
/* Returns one component, zero at end, or an error. The name is borrowed until
   the next mutation. PATH_ROOT means reset to the caller's root. The caller
   does directory search checks and resolves each component before advancing. */
isize fspathnext(Fspath *walk, Pathpart *part);
/* Replace the most recent PATH_NAME with a stable symlink target. For a
   relative target the caller keeps the directory that contained that name. */
isize fspathexpand(Fspath *walk, const char *target, u64 bytes);
#endif
