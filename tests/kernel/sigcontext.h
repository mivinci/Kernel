#ifndef _TEST_SIGCONTEXT_H
#define _TEST_SIGCONTEXT_H
#include <hal/context.h>
#include <abi/signal.h>
#include <diag.h>
#define CTX_ASSERT(c)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGCTXFAIL line=");                                                                  \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("signal context validation");                                                          \
    }                                                                                              \
  } while (0)
static void
contextcopy(Hctx *to, const Hctx *from)
{
  volatile u8       *destination = (volatile u8 *) to;
  const volatile u8 *source = (const volatile u8 *) from;
  u32                i;
  for (i = 0; i < sizeof(*to); i++)
    destination[i] = source[i];
}
static int
contextequal(const Hctx *a, const Hctx *b)
{
  const volatile u8 *first = (const volatile u8 *) a, *second = (const volatile u8 *) b;
  u32                i;
  for (i = 0; i < sizeof(*a); i++)
    if (first[i] != second[i])
      return 0;
  return 1;
}
static void
contextreject(const Hctx *trusted, const Hctx *candidate)
{
  Hctx         output;
  volatile u8 *bytes = (volatile u8 *) &output;
  u32          i;
  for (i = 0; i < sizeof(output); i++)
    bytes[i] = 0xa5;
  CTX_ASSERT(hal_signal_restore(&output, trusted, candidate) == HAL_CONTEXT_BAD);
  for (i = 0; i < sizeof(output); i++)
    CTX_ASSERT(bytes[i] == 0xa5);
}
#endif
