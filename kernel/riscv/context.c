#include <hal/context.h>
void
hal_user_init(Hctx *context, const Hinit *initial)
{
  u8 *byte = (u8 *) context;
  u32 i;
  for (i = 0; i < sizeof(*context); i++)
    byte[i] = 0;
  context->tf.pc = initial->entry;
  context->tf.x[2] = initial->sp;
  context->tf.x[10] = initial->arg;
  context->tf.status = 0x6000; /* FS Dirty, U mode, interrupts disabled. */
}
void
hal_trap_decode(const Hctx *context, Htrap *trap)
{
  u64 code = context->tf.cause;
  trap->code = code;
  trap->address = context->tf.address;
  trap->kind = HT_OTHER;
  trap->fromuser = !(context->tf.status & 0x100);
  trap->syscall_nr = context->tf.x[17];
  if (code >> 63)
    trap->kind = (code == ((1UL << 63) | 5)) ? HT_TIMER : HT_IRQ;
  else if (code == 8)
    trap->kind = HT_SYSCALL;
  else if (code == 12 || code == 13 || code == 15)
    trap->kind = HT_PAGEFAULT;
  else if (code == 2)
    trap->kind = HT_ILLEGAL;
}

void
hal_ctx_init(Hkctx *context, u64 top, void (*entry)(void))
{
  u8 *bytes = (u8 *) context;
  u32 i;
  for (i = 0; i < sizeof(*context); i++)
    bytes[i] = 0;
  context->sp = top;
  context->ra = (u64) entry;
}

u64
hal_syscall_arg(const Hctx *context, u32 slot)
{
  return slot < HAL_SYSCALL_ARGS ? context->tf.x[10 + slot] : 0;
}
void
hal_syscall_finish(Hctx *context, isize status, const u64 outputs[7])
{
  u32 i;
  context->tf.x[10] = (u64) status;
  for (i = 0; i < 7; i++)
    context->tf.x[11 + i] = status < 0 ? 0 : outputs[i];
  context->tf.x[5] = 0;
  context->tf.pc += 4;
}
