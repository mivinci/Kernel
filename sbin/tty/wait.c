#include "wait.h"
#define DECISECOND UINT64_C(100000000)

static int
valid(const Ttywait *wait)
{
  if (!wait || !IS_ALIGNED((usize) wait, 8) || wait->length > TTY_READ_BYTES ||
      wait->minimum > 255 || wait->minimum > wait->length || wait->time > 255 ||
      wait->canonical > 1 || wait->nonblock > 1 || wait->started > 1)
    return 0;
  if (!wait->length || wait->canonical || wait->nonblock || !wait->time)
    return !wait->deadline;
  return (wait->minimum && !wait->started) ? !wait->deadline : wait->deadline != 0;
}

isize
ttywaitinit(Ttywait *wait, const Termarg *mode, u32 length, u32 nonblock, u32 available, u64 now)
{
  Ttywait state;
  u64     interval;
  isize   error;
  if (!wait || !IS_ALIGNED((usize) wait, 8) || !mode || !IS_ALIGNED((usize) mode, 8) ||
      length > TTY_READ_BYTES || nonblock > 1 || available > TTY_INPUT_CAPACITY)
    return -ERR_INVALID;
  error = ttyattrcheck(mode, mode->ispeed);
  if (error)
    return error;
  state.length = length;
  state.minimum = MIN(mode->cc[TTY_VMIN], length);
  state.time = mode->cc[TTY_VTIME];
  state.canonical = !!(mode->lflag & TTY_ICANON);
  state.nonblock = nonblock;
  state.started = !!available;
  state.last = now;
  state.deadline = 0;
  if (length && !state.canonical && !nonblock && state.time && (!state.minimum || available)) {
    interval = state.time * DECISECOND;
    if (now > UINT64_MAX - interval)
      return -ERR_OVERFLOW;
    state.deadline = now + interval;
  }
  *wait = state;
  return 0;
}

isize
ttywaitbyte(Ttywait *wait, u64 now)
{
  u64 deadline = 0, interval;
  if (!valid(wait) || now < wait->last)
    return -ERR_STATE;
  if (wait->deadline && now >= wait->deadline)
    return 1;
  if (wait->length && !wait->canonical && !wait->nonblock && wait->minimum && wait->time) {
    interval = wait->time * DECISECOND;
    if (now > UINT64_MAX - interval)
      return -ERR_OVERFLOW;
    deadline = now + interval;
  }
  wait->last = now;
  wait->started = 1;
  if (deadline)
    wait->deadline = deadline;
  return 0;
}

isize
ttywaitcheck(Ttywait *wait, u32 available, u64 now, u64 *next)
{
  if (!next || !IS_ALIGNED((usize) next, 8) || available > TTY_INPUT_CAPACITY)
    return -ERR_INVALID;
  if (!valid(wait) || now < wait->last)
    return -ERR_STATE;
  wait->last = now;
  *next = 0;
  if (!wait->length)
    return 1;
  if (wait->canonical) {
    if (available)
      return 1;
    return wait->nonblock ? -ERR_WOULD_BLOCK : 0;
  }
  if (wait->nonblock)
    return available ? 1 : -ERR_WOULD_BLOCK;
  if ((!wait->minimum && (!wait->time || available)) ||
      (wait->minimum && available >= wait->minimum) || (wait->deadline && now >= wait->deadline))
    return 1;
  *next = wait->deadline;
  return 0;
}
