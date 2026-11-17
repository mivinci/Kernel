#include "../forkcontext.h"
#include <abi/syscall.h>
void
arch_forkcontext(Hctx *out, u64 pid, u64 fixva)
{
  Hctx parent, snapshot;
  u32  i;
  u64  sp = hal_user_sp(out);
  forkcontextcopy(&parent, out);
  for (i = 0; i < 32; i++)
    parent.tf.x[i] = 0x1000 + i;
  parent.tf.x[0] = 0;
  parent.tf.x[2] = sp;
  parent.tf.x[17] = SYS_FORKPREP;
  parent.tf.x[24] = fixva;
  parent.tf.x[27] = pid;
  parent.tf.pc = 0x10000;
  parent.tf.cause = 8;
  parent.tf.address = 0;
  parent.tf.status = 0x6000;
  parent.fp.f[31] = 0x5678;
  parent.fp.fcsr = 0x9f;
  parent.fp.reserved = 0;
  forkcontextcopy(&snapshot, &parent);
  FORKCTX_ASSERT(!hal_user_fork(out, &parent, pid, fixva) && forkcontextequal(&snapshot, &parent));
  FORKCTX_ASSERT(out->tf.pc == 0x10004 && hal_user_sp(out) == sp && out->tf.x[12] == pid &&
                 out->tf.x[13] == fixva && !out->tf.x[5]);
}
