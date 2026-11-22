#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

NORETURN void
__libc_assert(const char *expression, const char *file, int line, const char *function)
{
  char message[512];
  int  length = snprintf(message, sizeof(message), "%s:%d: %s: assertion failed: %s\n", file, line,
                         function, expression);
  if (length > 0)
    (void) write(2, message, MIN((size_t) length, sizeof(message) - 1));
  abort();
}
