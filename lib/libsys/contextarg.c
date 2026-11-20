#include "private/context.h"

static int
separate(const Rbank *bank, const Argsnap *out)
{
  u64 x = (u64) (uintptr_t) bank, y = (u64) (uintptr_t) out;
  return bank && out && sizeof(*bank) <= UINT64_MAX - x && sizeof(*out) <= UINT64_MAX - y &&
         (x + sizeof(*bank) <= y || y + sizeof(*out) <= x);
}

isize
sysctxarguments(Rbank *bank, Connpeer self, Rkey key, const char *const *argv,
                const char *const *envp, Argsnap *out)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (context->active)
    return -ERR_BUSY;
  if (context->phase != RCTX_PREPARED || context->input_saved)
    return -ERR_STATE;
  if (!separate(bank, out))
    return -ERR_INVALID;
  error = sysargcopy(bank->snapshots[key.layer], RCTX_SNAPSHOT_BYTES, argv, envp, out);
  if (error)
    return error;
  context->input_bytes = out->bytes;
  context->input_kind = 1;
  context->input_argc = out->argc;
  context->input_envc = out->envc;
  context->input_saved = 1;
  return 0;
}

isize
sysctxargumentview(Rbank *bank, Connpeer self, Rkey key, Argsnap *out)
{
  Rctx *context;
  isize error = sysctxlookup(bank, self, key, &context);
  if (error)
    return error;
  if (!separate(bank, out))
    return -ERR_INVALID;
  if (!context->input_saved || context->input_kind != 1)
    return -ERR_STATE;
  out->data = bank->snapshots[key.layer];
  out->bytes = context->input_bytes;
  out->argc = context->input_argc;
  out->envc = context->input_envc;
  out->argv_off = 0;
  out->env_off = out->envc ? out->argc * sizeof(Strref) : 0;
  return 0;
}
