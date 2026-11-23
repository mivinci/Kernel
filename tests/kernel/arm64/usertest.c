#include "ktest.h"
#include <diag.h>
extern const u8 user_begin[], user_call[], user_bootfault[], user_codefault[], user_kernelfault[],
    user_guardfault[];
void
arch_usercheck(Hctx *context, u32 phase)
{
  u32 i;
  u64 pc = 0x10000 + (u64) (user_call - user_begin);
  if (phase < 2) {
    for (i = 0; i < 31; i++)
      if (context->tf.x[i] != 0x100UL + i)
        panic("user integer registers");
    for (i = 0; i < 64; i++)
      if (context->fp.q[i] != (i % 2 ? 0x600UL : 0x500UL) + i / 2)
        panic("user SIMD registers");
    if (context->tf.sp != 0x3fffff0000UL || context->fp.fpsr != 0x1f ||
        context->fp.fpcr != 0x400000 || context->fp.tpidr != (phase ? 0x5678 : 0x1234))
      panic("user control");
    if (!phase)
      context->fp.tpidr = 0x5678;
    pc += (phase + 1) * 4;
  } else if (phase == 2)
    pc = 0x10000 + (u64) (user_bootfault - user_begin);
  else if (phase == 3)
    pc = 0x10000 + (u64) (user_codefault - user_begin);
  else if (phase == 4)
    pc = 0x10000 + (u64) (user_kernelfault - user_begin);
  else if (phase == 5)
    pc = 0x10000 + (u64) (user_guardfault - user_begin);
  else
    pc = 0x3ffffefff0UL;
  if ((context->tf.cause >> 26) != (phase < 2 ? 0x15UL : phase < 6 ? 0x24UL : 0x20UL))
    panic("user trap cause");
  if (context->tf.pc != pc)
    panic("user trap PC");
  if (phase >= 2)
    context->tf.pc += 4;
}
