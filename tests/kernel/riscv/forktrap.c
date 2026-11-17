#include <hal/context.h>
#include <abi/syscall.h>
void
arch_forktrap(Hctx *context)
{
  context->tf.pc = 0x10000;
  context->tf.cause = 8;
  context->tf.x[17] = SYS_FORKPREP;
}
