#include "pipe.h"

void
pipeinit(Pipe *pipe)
{
  pipe->refs[PIPE_READ] = pipe->refs[PIPE_WRITE] = 1;
  pipe->head = pipe->bytes = 0;
}

isize
pipehold(Pipe *pipe, u32 end)
{
  if (!pipe || end > PIPE_WRITE)
    return -ERR_INVALID;
  if (!pipe->refs[end])
    return -ERR_STALE;
  if (pipe->refs[end] == UINT32_MAX)
    return -ERR_OVERFLOW;
  pipe->refs[end]++;
  return 0;
}

isize
pipedrop(Pipe *pipe, u32 end)
{
  if (!pipe || end > PIPE_WRITE)
    return -ERR_INVALID;
  if (!pipe->refs[end])
    return -ERR_STALE;
  if (!--pipe->refs[end] && end == PIPE_READ)
    pipe->head = pipe->bytes = 0;
  return 0;
}

isize
piperead(Pipe *pipe, void *out, u32 bytes)
{
  u8 *target = out;
  u32 i;
  if (!pipe || (bytes && !out))
    return -ERR_INVALID;
  if (!pipe->refs[PIPE_READ])
    return -ERR_STALE;
  if (!bytes)
    return 0;
  if (!pipe->bytes && pipe->refs[PIPE_WRITE])
    return -ERR_WOULD_BLOCK;
  bytes = MIN(bytes, pipe->bytes);
  for (i = 0; i < bytes; i++)
    target[i] = pipe->data[(pipe->head + i) % PIPE_CAPACITY];
  pipe->head = (pipe->head + bytes) % PIPE_CAPACITY;
  pipe->bytes -= bytes;
  return bytes;
}

isize
pipewrite(Pipe *pipe, const void *data, u32 bytes)
{
  const u8 *source = data;
  u32       i, room, tail;
  if (!pipe || (bytes && !data))
    return -ERR_INVALID;
  if (!pipe->refs[PIPE_WRITE])
    return -ERR_STALE;
  if (!bytes)
    return 0;
  if (!pipe->refs[PIPE_READ])
    return -ERR_PEER;
  room = PIPE_CAPACITY - pipe->bytes;
  if (!room || (bytes <= PIPE_ATOMIC && room < bytes))
    return -ERR_WOULD_BLOCK;
  bytes = MIN(bytes, room);
  tail = pipe->head + pipe->bytes;
  for (i = 0; i < bytes; i++)
    pipe->data[(tail + i) % PIPE_CAPACITY] = source[i];
  pipe->bytes += bytes;
  return bytes;
}
