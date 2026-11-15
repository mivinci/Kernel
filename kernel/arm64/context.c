#include <hal/context.h>
void
hal_user_init(Hctx *context, const Hinit *initial)
{
  u8 *byte = (u8 *) context;
  u32 i;
  for (i = 0; i < sizeof(*context); i++)
    byte[i] = 0;
  context->tf.pc = initial->entry;
  context->tf.sp = initial->sp;
  context->tf.x[0] = initial->arg;
  context->tf.status = 0x3c0; /* EL0t, interrupts masked until controller setup. */
}
void
hal_trap_decode(const Hctx *context, Htrap *trap)
{
  u64 code = context->tf.cause;
  trap->code = code;
  trap->address = context->tf.address;
  trap->kind = HT_OTHER;
  trap->fromuser = (context->tf.status & 15) == 0;
  trap->syscall_nr = context->tf.x[8];
  code >>= 26;
  if (code == 0x15)
    trap->kind = HT_SYSCALL;
  else if (code == 0x20 || code == 0x21 || code == 0x24 || code == 0x25)
    trap->kind = HT_PAGEFAULT;
  else if (code == 0)
    trap->kind = HT_ILLEGAL;
}
