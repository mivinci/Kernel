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
  if (code == (1UL << 63)) {
    trap->kind = HT_IRQ;
    return;
  }
  code >>= 26;
  if (code == 0x15)
    trap->kind = HT_SYSCALL;
  else if (code == 0x20 || code == 0x21 || code == 0x24 || code == 0x25)
    trap->kind = HT_PAGEFAULT;
  else if (code == 0)
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
  context->x[11] = (u64) entry;
}

u64
hal_syscall_arg(const Hctx *context, u32 slot)
{
  return slot < HAL_SYSCALL_ARGS ? context->tf.x[slot] : 0;
}
void
hal_syscall_finish(Hctx *context, isize status, const u64 outputs[7])
{
  u32 i;
  context->tf.x[0] = (u64) status;
  for (i = 0; i < 7; i++)
    context->tf.x[1 + i] = status < 0 ? 0 : outputs[i];
  context->tf.x[9] = 0;
}

void
hal_user_irq(Hctx *context, u32 enabled)
{
  context->tf.status = (context->tf.status & ~128UL) | (enabled ? 0 : 128UL);
}

void
hal_ipc_finish(Hctx *context, isize status, const u64 outputs[8])
{
  hal_syscall_finish(context, status, outputs);
  context->tf.x[9] = status < 0 ? 0 : outputs[7];
  context->tf.x[3] = outputs[2];
}
