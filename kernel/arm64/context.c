#include <hal/context.h>
#include <abi/boot.h>
#include <abi/signal.h>
#include <abi/syscall.h>
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
  u32 fault = (u32) (code & 63);
  trap->code = code;
  trap->address = context->tf.address;
  trap->kind = HT_OTHER;
  trap->fromuser = (context->tf.status & 31) == 0;
  trap->syscall_nr = context->tf.x[8];
  if (code == (1UL << 63)) {
    trap->kind = HT_IRQ;
    return;
  }
  /* ISS2 occupies higher ESR bits; only EC[31:26] selects the class. */
  code = (code >> 26) & 63;
  if (code == 0x15)
    trap->kind = HT_SYSCALL;
  else if (code == 0x20 || code == 0x21 || code == 0x24 || code == 0x25) {
    if (fault <= 15)
      trap->kind = HT_PAGEFAULT;
    else if ((code == 0x24 || code == 0x25) && fault == 0x21)
      trap->kind = HT_ALIGN;
    else if ((code == 0x24 || code == 0x25) && fault == 0x11)
      trap->kind = HT_ACCESS;
    else if (fault == 0x10 || (fault >= 0x14 && fault <= 0x18) || (fault >= 0x1c && fault <= 0x1f))
      trap->kind = HT_BUS;
  } else if (code == 0x22 || code == 0x26)
    trap->kind = HT_ALIGN;
  else if (code == 0x2c)
    trap->kind = HT_ARITH;
  else if (code == 0 || code == 0x0e)
    trap->kind = HT_ILLEGAL;
}

u32
hal_fault_reason(const Hctx *context)
{
  Htrap trap;
  u64   code = context->tf.cause;
  hal_trap_decode(context, &trap);
  if (trap.kind == HT_PAGEFAULT)
    return SIGNAL_FAULT_MAPERR;
  if (trap.kind == HT_ACCESS)
    return SIGNAL_FAULT_ACCERR;
  if (trap.kind == HT_ALIGN)
    return SIGNAL_FAULT_ALIGN;
  if (trap.kind == HT_BUS)
    return SIGNAL_FAULT_BUSERR;
  if (trap.kind == HT_ILLEGAL)
    return SIGNAL_FAULT_ILLEGAL;
  if (trap.kind == HT_ARITH) {
    /* ESR.TFV guards the exception flags. Prefer the primary IEEE cause
       over accompanying inexact; denormal/unspecified traps stay OTHER. */
    if (code & (UINT64_C(1) << 23)) {
      if (code & 1)
        return SIGNAL_FAULT_FP_INVALID;
      if (code & 2)
        return SIGNAL_FAULT_FP_DIVZERO;
      if (code & 4)
        return SIGNAL_FAULT_FP_OVERFLOW;
      if (code & 8)
        return SIGNAL_FAULT_FP_UNDERFLOW;
      if (code & 16)
        return SIGNAL_FAULT_FP_INEXACT;
    }
    return SIGNAL_FAULT_FP_OTHER;
  }
  return SIGNAL_FAULT_UNKNOWN;
}

u64
hal_fault_address(const Hctx *context)
{
  Htrap trap;
  u64   ec = (context->tf.cause >> 26) & 63;
  hal_trap_decode(context, &trap);
  if (ec == 0x26)
    return context->tf.sp;
  if (ec == 0x22 || trap.kind == HT_ILLEGAL || trap.kind == HT_ARITH || trap.kind == HT_OTHER)
    return context->tf.pc;
  if ((ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) && (context->tf.cause & 0x400))
    return 0;
  return trap.address;
}

u32
hal_user_writefault(const Hctx *context)
{
  u64 code = context->tf.cause;
  return !(context->tf.status & 31) && ((code >> 26) & 63) == 0x24 && (code & 63) == 15 &&
         (code & 64) && !(code & 0x580);
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
hal_syscall_result(Hctx *context, isize status, const u64 outputs[7])
{
  hal_syscall_finish(context, status, outputs);
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

int
hal_user_fork(Hctx *out, const Hctx *parent, u64 pid, u64 fixva)
{
  u32 i;
  if (!out || !parent || out == parent || pid <= 1 || handlekind((Handle) pid) != HANDLE_PROCESS ||
      (fixva && (fixva < BOOT_USER_LO || fixva >= BOOT_USER_HI || (fixva & 7))) ||
      ((parent->tf.cause >> 26) & 63) != 0x15 || parent->tf.x[8] != SYS_FORKPREP ||
      parent->tf.pc < BOOT_USER_LO + 4)
    return HAL_CONTEXT_BAD;
  if (hal_signal_restore(out, parent, parent))
    return HAL_CONTEXT_BAD;
  out->tf.x[0] = out->tf.x[1] = 0;
  out->tf.x[2] = pid;
  out->tf.x[3] = fixva;
  for (i = 4; i <= 7; i++)
    out->tf.x[i] = 0;
  out->tf.x[9] = 0;
  return 0;
}
void
hal_user_forkfix(Hctx *child, u64 fixva)
{
  child->tf.x[3] = fixva;
}
