#ifndef _TEST_FORKCONTEXT_H
#define _TEST_FORKCONTEXT_H
#include <hal/context.h>
#include <diag.h>
#define FORKCTX_ASSERT(c)                                                                          \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("fork user context");                                                                  \
  } while (0)
static void
forkcontextcopy(Hctx *out, const Hctx *in)
{
  volatile u8       *to = (volatile u8 *) out;
  const volatile u8 *from = (const volatile u8 *) in;
  u32                i;
  for (i = 0; i < sizeof(*out); i++)
    to[i] = from[i];
}
static int
forkcontextequal(const Hctx *a, const Hctx *b)
{
  const volatile u8 *first = (const volatile u8 *) a, *second = (const volatile u8 *) b;
  u32                i;
  for (i = 0; i < sizeof(*a); i++)
    if (first[i] != second[i])
      return 0;
  return 1;
}
void arch_forkcontext(Hctx *out, u64 pid, u64 fixva);
#endif
