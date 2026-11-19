#include "term.h"

isize
ttyattrcheck(const Termarg *arg, u64 speed)
{
  u32 i;
  if (!arg || !IS_ALIGNED((usize) arg, 8))
    return -ERR_INVALID;
  if ((arg->iflag & ~(u64) TTY_ICRNL) || (arg->oflag & ~(u64) (TTY_OPOST | TTY_ONLCR)) ||
      arg->cflag != TTY_CS8 || (arg->lflag & ~(u64) (TTY_ICANON | TTY_ECHO | TTY_ISIG)) ||
      arg->ispeed != speed || arg->ospeed != speed)
    return -ERR_NOTSUP;
  for (i = 8; i < ARRAY_SIZE(arg->cc); i++)
    if (arg->cc[i])
      return -ERR_NOTSUP;
  return 0;
}

static int
overlap(u64 left, u32 bytes, u64 right, u32 length)
{
  return bytes && length && left < right + length && right < left + bytes;
}

isize
ttyencode(u64 oflag, const void *input, u32 bytes, void *output, u32 capacity, u32 *consumed)
{
  const u8 *source = input;
  u8       *target = output;
  u64       in = (u64) input, out = (u64) output, count = (u64) consumed;
  u32       i, written, need;
  if (!consumed || !IS_ALIGNED((usize) consumed, sizeof(*consumed)) || bytes > TTY_BATCH ||
      capacity > 2 * TTY_BATCH || (bytes && !input) || (capacity && !output) ||
      in > UINT64_MAX - bytes || out > UINT64_MAX - capacity ||
      count > UINT64_MAX - sizeof(*consumed) || overlap(in, bytes, out, capacity) ||
      overlap(in, bytes, count, sizeof(*consumed)) ||
      overlap(out, capacity, count, sizeof(*consumed)))
    return -ERR_INVALID;
  if (oflag & ~(u64) (TTY_OPOST | TTY_ONLCR))
    return -ERR_NOTSUP;
  written = 0;
  for (i = 0; i < bytes; i++) {
    need =
        source[i] == '\n' && (oflag & (TTY_OPOST | TTY_ONLCR)) == (TTY_OPOST | TTY_ONLCR) ? 2 : 1;
    if (need > capacity - written)
      break;
    if (need == 2)
      target[written++] = '\r';
    target[written++] = source[i];
  }
  *consumed = i;
  return (isize) written;
}
