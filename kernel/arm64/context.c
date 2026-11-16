#include <hal/context.h>
#include <abi/boot.h>
#include <abi/signal.h>
STATIC_ASSERT(SIGNAL_FRAME_TF + sizeof(Hctx) == SIGNAL_FRAME_ARM_BYTES, signal_context_frame_size);
STATIC_ASSERT(SIGNAL_FRAME_TF + offsetof(Hctx, fp) == SIGNAL_FRAME_FP, signal_context_fp_offset);
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

u64
hal_user_sp(const Hctx *context)
{
  return context->tf.sp;
}
static int
signaladdress(u64 pc, u64 sp)
{
  return pc >= BOOT_USER_LO && pc < BOOT_USER_HI && !(pc & (HAL_ENTRY_ALIGN - 1)) &&
         sp >= BOOT_USER_LO && sp <= BOOT_USER_HI && !(sp & 15);
}
#define SIGNAL_NZCV      0xf0000000UL
#define SIGNAL_FPCR_EDIT 0x03c00000U
#define SIGNAL_FPSR_EDIT 0x0800009fU
int
hal_signal_enter(Hctx *context, u64 frame, u64 trampoline, u64 handler, u64 flags, u32 signum)
{
  if (!context || (context->tf.status & 31) || !signaladdress(trampoline, frame) ||
      !signaladdress(handler, frame) || frame > BOOT_USER_HI - SIGNAL_FRAME_TF - sizeof(Hctx))
    return HAL_CONTEXT_BAD;
  context->tf.pc = trampoline;
  context->tf.sp = frame;
  context->tf.x[30] = 0;
  context->tf.x[0] = signum;
  context->tf.x[1] = frame + sizeof(Sighead);
  context->tf.x[2] = frame + SIGNAL_FRAME_TF;
  context->tf.x[3] = handler;
  context->tf.x[4] = frame;
  context->tf.x[5] = flags;
  return 0;
}
int
hal_signal_restore(Hctx *out, const Hctx *trusted, const Hctx *candidate)
{
  u64       status, cause, address;
  u32       i, fpcr, fpsr;
  u8       *destination;
  const u8 *source;
  if (!out || !trusted || !candidate || (trusted->tf.status & 31) ||
      ((candidate->tf.status ^ trusted->tf.status) & ~SIGNAL_NZCV) ||
      candidate->tf.cause != trusted->tf.cause || candidate->tf.address != trusted->tf.address ||
      !signaladdress(candidate->tf.pc, candidate->tf.sp) || candidate->fp.reserved ||
      trusted->fp.reserved || ((candidate->fp.fpcr ^ trusted->fp.fpcr) & ~SIGNAL_FPCR_EDIT) ||
      ((candidate->fp.fpsr ^ trusted->fp.fpsr) & ~SIGNAL_FPSR_EDIT))
    return HAL_CONTEXT_BAD;
  /* Keep every other original legal FP control/status bit, rather than
     rejecting or silently zeroing a non-default trusted environment. */
  status = ((trusted->tf.status & ~SIGNAL_NZCV) | (candidate->tf.status & SIGNAL_NZCV)) & ~31UL;
  cause = trusted->tf.cause;
  address = trusted->tf.address;
  fpcr = (trusted->fp.fpcr & ~SIGNAL_FPCR_EDIT) | (candidate->fp.fpcr & SIGNAL_FPCR_EDIT);
  fpsr = (trusted->fp.fpsr & ~SIGNAL_FPSR_EDIT) | (candidate->fp.fpsr & SIGNAL_FPSR_EDIT);
  destination = (u8 *) out;
  source = (const u8 *) candidate;
  for (i = 0; i < sizeof(*out); i++)
    destination[i] = source[i];
  out->tf.status = status;
  out->tf.cause = cause;
  out->tf.address = address;
  out->fp.fpcr = fpcr;
  out->fp.fpsr = fpsr;
  out->fp.reserved = 0;
  return 0;
}
