#include <hal/context.h>
#include <abi/boot.h>
#include <abi/signal.h>
#include <abi/syscall.h>
STATIC_ASSERT(SIGNAL_FRAME_TF + sizeof(Hctx) == SIGNAL_FRAME_RV_BYTES, signal_context_frame_size);
STATIC_ASSERT(SIGNAL_FRAME_TF + offsetof(Hctx, fp) == SIGNAL_FRAME_FP, signal_context_fp_offset);
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

u32
hal_user_writefault(const Hctx *context)
{
  return !(context->tf.status & 0x100) && context->tf.cause == 15;
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

void
hal_user_irq(Hctx *context, u32 enabled)
{
  context->tf.status = (context->tf.status & ~32UL) | (enabled ? 32UL : 0);
}

void
hal_ipc_finish(Hctx *context, isize status, const u64 outputs[8])
{
  hal_syscall_finish(context, status, outputs);
  context->tf.x[5] = status < 0 ? 0 : outputs[7];
  context->tf.x[13] = outputs[2];
}

u64
hal_user_sp(const Hctx *context)
{
  return context->tf.x[2];
}
static int
signaladdress(u64 pc, u64 sp)
{
  return pc >= BOOT_USER_LO && pc < BOOT_USER_HI && !(pc & (HAL_ENTRY_ALIGN - 1)) &&
         sp >= BOOT_USER_LO && sp <= BOOT_USER_HI && !(sp & 15);
}
int
hal_signal_enter(Hctx *context, u64 frame, u64 trampoline, u64 handler, u64 flags, u32 signum)
{
  if (!context || (context->tf.status & 0x102) || !signaladdress(trampoline, frame) ||
      !signaladdress(handler, frame) || frame > BOOT_USER_HI - SIGNAL_FRAME_TF - sizeof(Hctx))
    return HAL_CONTEXT_BAD;
  context->tf.pc = trampoline;
  context->tf.x[2] = frame;
  context->tf.x[1] = 0;
  context->tf.x[10] = signum;
  context->tf.x[11] = frame + sizeof(Sighead);
  context->tf.x[12] = frame + SIGNAL_FRAME_TF;
  context->tf.x[13] = handler;
  context->tf.x[14] = frame;
  context->tf.x[15] = flags;
  return 0;
}
int
hal_signal_restore(Hctx *out, const Hctx *trusted, const Hctx *candidate)
{
  u64       status, cause, address, fcsr, rounding;
  u32       i;
  u8       *destination;
  const u8 *source;
  if (!out || !trusted || !candidate || (trusted->tf.status & 0x102) || trusted->tf.x[0] ||
      candidate->tf.x[0] || candidate->tf.status != trusted->tf.status ||
      candidate->tf.cause != trusted->tf.cause || candidate->tf.address != trusted->tf.address ||
      !signaladdress(candidate->tf.pc, candidate->tf.x[2]) || candidate->fp.reserved ||
      trusted->fp.reserved || ((candidate->fp.fcsr ^ trusted->fp.fcsr) & ~0xffUL))
    return HAL_CONTEXT_BAD;
  fcsr = candidate->fp.fcsr;
  rounding = (fcsr >> 5) & 7;
  if (rounding > 4 && rounding != ((trusted->fp.fcsr >> 5) & 7))
    return HAL_CONTEXT_BAD;
  status = trusted->tf.status & ~0x102UL;
  cause = trusted->tf.cause;
  address = trusted->tf.address;
  destination = (u8 *) out;
  source = (const u8 *) candidate;
  for (i = 0; i < sizeof(*out); i++)
    destination[i] = source[i];
  out->tf.x[0] = 0;
  out->tf.status = status;
  out->tf.cause = cause;
  out->tf.address = address;
  out->fp.fcsr = fcsr;
  out->fp.reserved = 0;
  return 0;
}

int
hal_user_fork(Hctx *out, const Hctx *parent, u64 pid, u64 fixva)
{
  u32 i;
  if (!out || !parent || out == parent || pid <= 1 || handlekind((Handle) pid) != HANDLE_PROCESS ||
      (fixva && (fixva < BOOT_USER_LO || fixva >= BOOT_USER_HI || (fixva & 7))) ||
      parent->tf.cause != 8 || parent->tf.x[17] != SYS_FORKPREP ||
      parent->tf.pc >= BOOT_USER_HI - 4)
    return HAL_CONTEXT_BAD;
  if (hal_signal_restore(out, parent, parent))
    return HAL_CONTEXT_BAD;
  out->tf.x[10] = out->tf.x[11] = 0;
  out->tf.x[12] = pid;
  out->tf.x[13] = fixva;
  for (i = 14; i <= 17; i++)
    out->tf.x[i] = 0;
  out->tf.x[5] = 0;
  out->tf.pc += 4;
  return 0;
}
