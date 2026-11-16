#include "sigcontext.h"
void
arch_sigcontext(Hctx *context, u32 mode)
{
  Hctx trusted, candidate, entered;
  u64  frame = 0x3ffffee000UL;
  contextcopy(&trusted, context);
  contextcopy(&entered, &trusted);
  CTX_ASSERT(!hal_signal_enter(&entered, frame, 0x10000, 0x10004, SIGNAL_SIGINFO, SIGNAL_USR1));
  CTX_ASSERT(entered.tf.pc == 0x10000 && entered.tf.sp == frame && !entered.tf.x[30] &&
             entered.tf.x[0] == SIGNAL_USR1 && entered.tf.x[1] == frame + 64 &&
             entered.tf.x[2] == frame + 128 && entered.tf.x[3] == 0x10004 &&
             entered.tf.x[4] == frame && entered.tf.x[5] == SIGNAL_SIGINFO &&
             entered.tf.status == trusted.tf.status && entered.fp.fpcr == trusted.fp.fpcr);
  contextcopy(&candidate, &entered);
  CTX_ASSERT(hal_signal_enter(&entered, frame + 1, 0x10000, 0x10004, 0, SIGNAL_INT) ==
                 HAL_CONTEXT_BAD &&
             contextequal(&candidate, &entered));
  contextcopy(&candidate, &trusted);
  candidate.tf.status ^= 5;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.status ^= 128;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.tf.sp |= 1;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.fp.fpcr = 1U << 8;
  contextreject(&trusted, &candidate);
  contextcopy(&candidate, &trusted);
  candidate.fp.fpsr = 1U << 5;
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
  candidate.tf.status |= 0xa0000000UL;
  candidate.tf.x[19] = 0x12345678;
  candidate.fp.q[62] = candidate.fp.q[63] = 0x5678;
  candidate.fp.fpcr = mode ? 0x01800000U : 0x03c00000U;
  candidate.fp.fpsr = 0x0800009fU;
  candidate.tf.x[6] = candidate.fp.fpcr;
  candidate.tf.x[7] = candidate.fp.fpsr;
  CTX_ASSERT(!hal_signal_restore(context, &trusted, &candidate) &&
             contextequal(context, &candidate));
}
