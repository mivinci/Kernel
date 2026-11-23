#include "../forkcontext.h"
#include <abi/syscall.h>
void
arch_forkcontext(Hctx *out, u64 pid, u64 fixva)
{
  Hctx parent, snapshot;
  u32  i;
  forkcontextcopy(&parent, out);
  for (i = 0; i < 31; i++)
    parent.tf.x[i] = 0x1000 + i;
  parent.tf.x[8] = SYS_FORKPREP;
  parent.tf.x[19] = pid;
  parent.tf.x[21] = fixva;
  parent.tf.pc = 0x10004;
  parent.tf.cause = (u64) 0x15 << 26;
  parent.tf.address = 0;
  parent.tf.status = 0xa0000000;
  parent.fp.q[62] = parent.fp.q[63] = 0x5678;
  parent.fp.fpcr = 0x01800000;
  parent.fp.fpsr = 0x0800009f;
  parent.fp.tpidr = 0;
  forkcontextcopy(&snapshot, &parent);
  FORKCTX_ASSERT(!hal_user_fork(out, &parent, pid, fixva) && forkcontextequal(&snapshot, &parent));
  FORKCTX_ASSERT(out->tf.pc == 0x10004 && out->tf.x[2] == pid && out->tf.x[3] == fixva &&
                 !out->tf.x[9]);
}
