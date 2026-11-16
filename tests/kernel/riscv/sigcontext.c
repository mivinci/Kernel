#include "sigcontext.h"
void
arch_sigcontext(Hctx *context, u32 mode)
{
  Hctx trusted, candidate, entered;
  u64  frame = 0x3ffffee000UL;
  contextcopy(&trusted, context);
  trusted.fp.fcsr = 0xff;
  contextcopy(&entered, &trusted);
  CTX_ASSERT(!hal_signal_enter(&entered, frame, 0x10000, 0x10004, SIGNAL_SIGINFO, SIGNAL_USR1));
  CTX_ASSERT(entered.tf.pc == 0x10000 && entered.tf.x[2] == frame && !entered.tf.x[1] &&
             entered.tf.x[10] == SIGNAL_USR1 && entered.tf.x[11] == frame + 64 &&
             entered.tf.x[12] == frame + 128 && entered.tf.x[13] == 0x10004 &&
             entered.tf.x[14] == frame && entered.tf.x[15] == SIGNAL_SIGINFO &&
             entered.tf.status == trusted.tf.status && entered.fp.fcsr == 0xff);
  contextcopy(&candidate, &entered);
  CTX_ASSERT(hal_signal_enter(&entered, frame + 1, 0x10000, 0x10004, 0, SIGNAL_INT) ==
                 HAL_CONTEXT_BAD &&
             contextequal(&candidate, &entered));
  contextcopy(&candidate, &trusted);
  candidate.tf.status ^= 0x100;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.status ^= 32;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.x[0] = 1;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.x[2] |= 1;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.fp.fcsr = (6UL << 5) | 31;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.fp.reserved = 1;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.pc = 0x10001;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.address = 1;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.x[9] = 0x12345678;
  candidate.fp.f[31] = 0x5678;
  candidate.fp.fcsr = mode ? 0xff : 0x9f;
  candidate.tf.x[16] = candidate.fp.fcsr;
  CTX_ASSERT(!hal_signal_restore(context, &trusted, &candidate) &&
             contextequal(context, &candidate));
}
