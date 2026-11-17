#include <hal/context.h>
#include <abi/syscall.h>
void
arch_forktrap(Hctx *context)
{
  context->tf.pc = 0x10004;
  context->tf.cause = (u64) 0x15 << 26;
  context->tf.x[8] = SYS_FORKPREP;
}
