#include "private/context.h"
#include <abi/syscall.h>
STATIC_ASSERT(FIX_LAYER_COUNT == RCTX_LAYERS, fix_context_layer_count);
static int
identity(Connpeer self)
{
  return handlekind(self.pid) == HANDLE_PROCESS && self.image;
}
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static void
clear(Rctx *context)
{
  u32          i;
  volatile u8 *bytes = (volatile u8 *) context;
  for (i = 0; i < sizeof(*context); i++)
    bytes[i] = 0;
}
static isize
source(Rbank *bank, Connpeer self)
{
  if (!bank || !identity(self))
    return -ERR_INVALID;
  return bank->self.pid == self.pid && bank->self.image == self.image ? 0 : -ERR_STALE;
}
isize
sysctxlookup(Rbank *bank, Connpeer self, Rkey key, Rctx **out)
{
  Rctx *context;
  isize error = source(bank, self);
  if (error)
    return error;
  if (key.reserved || key.layer >= RCTX_LAYERS || !key.sequence)
    return -ERR_INVALID;
  context = &bank->contexts[key.layer];
  if (context->sequence != key.sequence || context->phase == RCTX_FREE)
    return -ERR_STALE;
  if (context->phase > RCTX_CLEANING || context->active > 1)
    return -ERR_STATE;
  if (context->sequence > bank->sequence)
    return -ERR_STATE;
  if (context->input_saved > 1 || context->input_bytes > RCTX_SNAPSHOT_BYTES ||
      (!context->input_saved && context->input_bytes))
    return -ERR_STATE;
  if (context->input_kind > 1 || context->input_reserved ||
      (context->input_kind == 0 && (context->input_argc || context->input_envc)) ||
      (context->input_kind == 1 &&
       (!context->input_saved || context->input_argc > BOOT_ARG_COUNT ||
        context->input_envc > BOOT_ARG_COUNT ||
        context->input_bytes <
            (u64) (context->input_argc + context->input_envc + 2) * sizeof(char *))))
    return -ERR_STATE;
  *out = context;
  return 0;
}
isize
sysctxfixcheck(Rbank *bank, Connpeer parent, const Fixlayer layers[FIX_LAYER_COUNT])
{
  u32   i;
  isize error = source(bank, parent);
  if (error)
    return error;
  if (!layers)
    return -ERR_INVALID;
  for (i = 0; i < RCTX_LAYERS; i++) {
    Rctx *context = &bank->contexts[i];
    Rkey  key;
    u64   action = layers[i].action;
    if (action == FIX_LAYER_EMPTY) {
      if (layers[i].sequence || context->phase != RCTX_FREE || context->active ||
          context->route.kind != RCTX_ROUTE_NONE)
        return -ERR_STATE;
      continue;
    }
    key.sequence = layers[i].sequence;
    key.layer = i;
    key.reserved = 0;
    error = sysctxlookup(bank, parent, key, &context);
    if (error)
      return error;
    if (context->route.kind > RCTX_ROUTE_OP)
      return -ERR_STATE;
    switch (action) {
    case FIX_LAYER_UNSENT:
      if (context->phase != RCTX_PREPARED)
        return -ERR_STATE;
      break;
    case FIX_LAYER_COMPLETE:
      if (context->phase != RCTX_DONE && context->phase != RCTX_CLEANING)
        return -ERR_STATE;
      break;
    case FIX_LAYER_INTERRUPT:
      if (context->phase < RCTX_WAITING)
        return -ERR_STATE;
      break;
    default:
      return -ERR_INVALID;
    }
  }
  return 0;
}
isize
sysctxfixraw(Rbank *bank, Connpeer parent, Connpeer child, const Fixlayer layers[FIX_LAYER_COUNT])
{
  u32   i;
  isize error;
  if (!bank || !identity(parent) || !identity(child) || parent.pid == child.pid || !layers)
    return -ERR_INVALID;
  if (!source(bank, child))
    return 0;
  error = sysctxfixcheck(bank, parent, layers);
  if (error)
    return error;
  for (i = 0; i < RCTX_LAYERS; i++)
    if (layers[i].action != FIX_LAYER_EMPTY &&
        (layers[i].action != FIX_LAYER_UNSENT || bank->contexts[i].active ||
         bank->contexts[i].route.kind != RCTX_ROUTE_NONE))
      return -ERR_NOTSUP;
  bank->self = child;
  return 0;
}
isize
sysctxinit(Rbank *bank, Connpeer self)
{
  u32 i;
  if (!bank || !identity(self))
    return -ERR_INVALID;
  bank->self.pid = self.pid;
  bank->self.image = self.image;
  bank->sequence = 0;
  for (i = 0; i < RCTX_LAYERS; i++)
    clear(&bank->contexts[i]);
  /* Snapshot arrays are private input workspaces, not initialized ownership. */
  return 0;
}
isize
sysctxprepare(Rbank *bank, Connpeer self, u32 layer, u64 number, const u64 args[7], Rkey *out)
{
  Rctx *context;
  u64   sequence;
  u64   saved[7];
  u32   i;
  isize error = source(bank, self);
  if (error)
    return error;
  if (layer >= RCTX_LAYERS || number == SYS_SIGRETURN ||
      !separate(bank, sizeof(*bank), out, sizeof(*out)))
    return -ERR_INVALID;
  context = &bank->contexts[layer];
  if (context->phase != RCTX_FREE || context->active)
    return -ERR_BUSY;
  if (bank->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  for (i = 0; i < 7; i++)
    saved[i] = args ? args[i] : 0;
  sequence = ++bank->sequence;
  clear(context);
  context->sequence = sequence;
  context->number = number;
  for (i = 0; i < 7; i++)
    context->args[i] = saved[i];
  context->phase = RCTX_PREPARED;
  out->sequence = sequence;
  out->layer = layer;
  out->reserved = 0;
  return 0;
}
isize
sysctxinput(Rbank *bank, Connpeer self, Rkey key, const void *input, u64 bytes)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_PREPARED || context->input_saved)
    return -ERR_STATE;
  if (bytes > RCTX_SNAPSHOT_BYTES)
    return -ERR_LIMIT;
  if (bytes && (!input || (u64) (uintptr_t) input > UINT64_MAX - bytes))
    return -ERR_FAULT;
  if (bytes && !separate(bank, sizeof(*bank), input, bytes))
    return -ERR_INVALID;
  if (bytes) {
    error = __sys_copy(bank->snapshots[key.layer], input, bytes);
    if (error)
      return error;
  }
  context->input_bytes = bytes;
  context->input_saved = 1;
  return 0;
}
isize
sysctxinputview(Rbank *bank, Connpeer self, Rkey key, const void **out, u64 *bytes)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), out, sizeof(*out)) ||
      !separate(bank, sizeof(*bank), bytes, sizeof(*bytes)) ||
      !separate(out, sizeof(*out), bytes, sizeof(*bytes)))
    return -ERR_INVALID;
  if (!context->input_saved)
    return -ERR_STATE;
  *out = bank->snapshots[key.layer];
  *bytes = context->input_bytes;
  return 0;
}
isize
sysctxrearm(Rbank *bank, Connpeer self, Rkey key, u64 number, const u64 args[7])
{
  Rctx *context;
  u64   saved[7];
  u32   i;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (number == SYS_SIGRETURN)
    return -ERR_INVALID;
  if (context->phase != RCTX_CLEANING || context->route.kind != RCTX_ROUTE_NONE)
    return -ERR_STATE;
  for (i = 0; i < 7; i++)
    saved[i] = args ? args[i] : 0;
  context->number = number;
  for (i = 0; i < 7; i++)
    context->args[i] = saved[i];
  for (i = 0; i < sizeof(context->result); i++)
    ((u8 *) &context->result)[i] = 0;
  context->status = 0;
  context->phase = RCTX_PREPARED;
  return 0;
}
isize
sysctxabort(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx *context;
  u64   sequence;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_PREPARED)
    return -ERR_STATE;
  sequence = context->sequence;
  clear(context);
  context->sequence = sequence;
  return 0;
}
isize
sysctxinvoke(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx *context;
  u64   old, ignored;
  isize status, error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &old);
  if (error)
    return error;
  error = sysctxlookup(bank, self, key, &context);
  if (!error && (context->phase != RCTX_PREPARED || context->active))
    error = -ERR_STATE;
  if (!error)
    context->active = 1;
  if (__sys_sigmask(SIGNAL_SETMASK, old, &ignored))
    return -ERR_STATE;
  if (error)
    return error;
  __sys_ctxenter(context);
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &old);
  if (error)
    return error;
  if (context->phase != RCTX_DONE && context->phase != RCTX_CLEANING) {
    __sys_sigmask(SIGNAL_SETMASK, old, &ignored);
    return -ERR_STATE;
  }
  status = context->status;
  /* After unmask, a handler can consume/finish the now inactive record. The
     native status is already in this C continuation, never reloaded from it. */
  context->active = 0;
  error = __sys_sigmask(SIGNAL_SETMASK, old, &ignored);
  return error ? error : status;
}
isize
sysctxcapture(Rbank *bank, Connpeer self, Rkey key, void *trap)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  return sysctxdecode(context, trap, (u64) (uintptr_t) __sys_ctxreturn_begin,
                      (u64) (uintptr_t) __sys_ctxreturn_end, (u64) (uintptr_t) __sys_ctxresume);
}
isize
sysctxresult(Rbank *bank, Connpeer self, Rkey key, isize *status, Sysret *out)
{
  Rctx *context;
  u32   i;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, sizeof(*bank), status, sizeof(*status)) ||
      !separate(bank, sizeof(*bank), out, sizeof(*out)) ||
      !separate(status, sizeof(*status), out, sizeof(*out)))
    return -ERR_INVALID;
  if (context->phase != RCTX_DONE || context->route.kind != RCTX_ROUTE_NONE)
    return -ERR_STATE;
  *status = context->status;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = ((const u8 *) &context->result)[i];
  context->phase = RCTX_CLEANING;
  return 0;
}
isize
sysctxfinish(Rbank *bank, Connpeer self, Rkey key)
{
  Rctx *context;
  u64   sequence;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_CLEANING)
    return -ERR_STATE;
  if (context->route.kind && context->route.delivered != 1)
    return -ERR_STATE;
  sequence = context->sequence;
  clear(context);
  context->sequence = sequence;
  return 0;
}
