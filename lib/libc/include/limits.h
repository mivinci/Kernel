#ifndef _LIBC_LIMITS_H
#define _LIBC_LIMITS_H

#include_next <limits.h>
#include <proto/path.h>

#define PATH_MAX FS_PATH_BYTES
#define NAME_MAX FS_NAME_BYTES

#endif
