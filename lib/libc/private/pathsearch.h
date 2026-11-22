#ifndef _LIBC_PATHSEARCH_H
#define _LIBC_PATHSEARCH_H
#include <types.h>

#define PATHSEARCH_BYTES 4096
#define PATHSEARCH_COUNT 32

/* file and path are stable private NUL-terminated snapshots, disjoint from
   scratch. attempt consumes the candidate only for the duration of the call
   and returns a positive errno or zero. A file containing slash is attempted
   directly. Neither the iterator nor the callback contract uses errno. */
int pathsearch(const char *file, const char *path, char scratch[PATHSEARCH_BYTES],
               int (*attempt)(void *, const char *), void *context);
#endif
