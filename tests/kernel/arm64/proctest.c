#include "ktest.h"
#include <diag.h>
extern const u8 proc_begin[], proc_call[];
void
arch_proccheck(Hctx *context)
{
  if (context->tf.x[0] != 42 || context->tf.pc != 0x10000 + (u64) (proc_call - proc_begin) + 4)
    panic("process user result");
}
