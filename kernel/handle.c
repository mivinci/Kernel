#include <handle.h>
u32
handletype(Handle handle)
{
  u64 word = (u64) handle;
  u32 kind;
  if (handle == 1)
    return HANDLE_PROCESS;
  if (handle <= 0 || !((word >> 16) & HANDLE_GENERATION_MAX))
    return 0;
  kind = (u32) (word >> 59);
  return kind >= HANDLE_PROCESS && kind <= HANDLE_TERMINAL ? kind : 0;
}
Handle
handlepack(u32 kind, u64 generation, u32 index)
{
  if (kind < HANDLE_PROCESS || kind > HANDLE_TERMINAL || !generation ||
      generation > HANDLE_GENERATION_MAX || index > 65535)
    return 0;
  return (Handle) (((u64) kind << 59) | (generation << 16) | index);
}
u64
handlegen(Handle handle)
{
  return handletype(handle) ? ((u64) handle >> 16) & HANDLE_GENERATION_MAX : 0;
}
u32
handleindex(Handle handle)
{
  return handle == 1 ? 0 : (u32) ((u64) handle & 65535);
}
