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
    for (i = 1; i < 32; i++)
      if (context->tf.x[i] != (i == 2 ? 0x3fffff0000UL : 0x100UL + i))
        panic("user integer registers");
    for (i = 0; i < 32; i++)
      if (context->fp.f[i] != 0x500 + i)
        panic("user FP registers");
    if (context->fp.fcsr != 0x21 || context->fp.reserved)
      panic("user FP control");
    pc += phase * 4;
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
  if (context->tf.cause != (phase < 2 ? 8UL : phase < 4 ? 15UL : phase < 6 ? 13UL : 12UL))
    panic("user trap cause");
  if (context->tf.pc != pc)
    panic("user trap PC");
  context->tf.pc += 4;
}
