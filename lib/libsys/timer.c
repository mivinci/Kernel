#include "private/timer.h"
#include "private/syscall.h"
#include <abi/time.h>
#include <abi/signal.h>
#include <abi/event.h>

isize
systimernow(u64 *out)
{
  const u64 second = UINT64_C(1000000000);
  int64_t   seconds;
  u64       nanos;
  isize     error;
  if (!out)
    return -ERR_INVALID;
  error = __sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos);
  if (error)
    return error;
  if (seconds < 0 || nanos >= second || (u64) seconds > (UINT64_MAX - nanos) / second)
    return -ERR_OVERFLOW;
  *out = (u64) seconds * second + nanos;
  return 0;
}

static int
earlier(const Timerjob *left, const Timerjob *right)
{
  return left->deadline < right->deadline ||
         (left->deadline == right->deadline && left->ticket < right->ticket);
}

static void
rise(Timerheap *heap, u32 index)
{
  Timerjob job = heap->jobs[index];
  while (index) {
    u32 parent = (index - 1) / 2;
    if (!earlier(&job, &heap->jobs[parent]))
      break;
    heap->jobs[index] = heap->jobs[parent];
    index = parent;
  }
  heap->jobs[index] = job;
}

static void
sink(Timerheap *heap, u32 index)
{
  Timerjob job = heap->jobs[index];
  u32      child = index * 2 + 1;
  while (child < heap->count) {
    if (child + 1 < heap->count && earlier(&heap->jobs[child + 1], &heap->jobs[child]))
      child++;
    if (!earlier(&heap->jobs[child], &job))
      break;
    heap->jobs[index] = heap->jobs[child];
    index = child;
    child = index * 2 + 1;
  }
  heap->jobs[index] = job;
}

isize
systimeradd(Timerheap *heap, u64 deadline, u64 *ticket)
{
  u64   generation;
  u32   index;
  isize error;
  if (!heap || !ticket)
    return -ERR_INVALID;
  if (heap->count >= SYSTIMER_LIMIT)
    return -ERR_CAPACITY;
  if (heap->serial == UINT64_MAX)
    return -ERR_OVERFLOW;
  if (!heap->count || deadline < heap->jobs[0].deadline) {
    error = __sys_alarmset(deadline, &generation);
    if (error)
      return error;
    heap->generation = generation;
  }
  index = heap->count++;
  heap->jobs[index].deadline = deadline;
  heap->jobs[index].ticket = ++heap->serial;
  rise(heap, index);
  *ticket = heap->serial;
  return 0;
}

static isize
removejob(Timerheap *heap, u32 index)
{
  u32   last = heap->count - 1, next = 1;
  u64   generation;
  isize error = 0;
  if (!index) {
    if (!last)
      error = __sys_alarmcancel(&generation);
    else {
      if (last >= 2 && earlier(&heap->jobs[2], &heap->jobs[1]))
        next = 2;
      if (heap->jobs[next].deadline == heap->jobs[0].deadline)
        generation = heap->generation;
      else
        error = __sys_alarmset(heap->jobs[next].deadline, &generation);
    }
    if (error)
      return error;
    heap->generation = generation;
  }
  heap->count = last;
  if (index != last) {
    heap->jobs[index] = heap->jobs[last];
    if (index && earlier(&heap->jobs[index], &heap->jobs[(index - 1) / 2]))
      rise(heap, index);
    else
      sink(heap, index);
  }
  return 0;
}

isize
systimercancel(Timerheap *heap, u64 ticket)
{
  u32 i;
  if (!heap || !ticket)
    return -ERR_INVALID;
  for (i = 0; i < heap->count; i++)
    if (heap->jobs[i].ticket == ticket)
      return removejob(heap, i);
  return -ERR_STALE;
}

isize
systimerdue(Timerheap *heap, u64 now, Timerjob *out)
{
  Timerjob job;
  isize    error;
  if (!heap || !out)
    return -ERR_INVALID;
  if (!heap->count || now < heap->jobs[0].deadline)
    return -ERR_WOULD_BLOCK;
  job = heap->jobs[0];
  error = removejob(heap, 0);
  if (error)
    return error;
  *out = job;
  return 0;
}

typedef struct Sleepwait Sleepwait;
struct Sleepwait
{
  volatile u64 ticket;
  u64          deadline, remaining;
  isize        result;
  u32          used;
};
static Timerheap sleepheap;
static int (*rqprogress)(void);

void
systimerbindrq(int (*progress)(void))
{
  rqprogress = progress;
}
static Sleepwait         waits[SIGNAL_DEPTH + 1], *pending;
typedef struct Eventwait Eventwait;
struct Eventwait
{
  volatile u64 ticket;
  isize        result;
  u32          timed;
};
static Eventwait *eventwait;
static isize      sleepfault;
extern const u8   __sys_sleepwait_checked[], __sys_sleepwait_return[], __sys_sleepwait_skip[];

static void
settle(Sleepwait *wait, isize result, u64 remaining)
{
  isize error = systimercancel(&sleepheap, wait->ticket);
  /* If the native alarm cannot be changed, retain the heap and stop arming
     new sleeps. The local continuation still receives the original error. */
  if (error)
    sleepfault = error;
  wait->remaining = remaining;
  wait->result = error ? error : result;
  wait->ticket = 0;
  pending = NULL;
}

int
syssleepwaiting(void)
{
  return pending != NULL || eventwait != NULL;
}

void
syssleepinterrupt(void *trap, void *outer, u64 flags)
{
  Sleepwait *wait = pending;
  u64        begin = (u64) __sys_sleepwait_checked, end = (u64) __sys_sleepwait_return;
  u64        resume = (u64) __sys_sleepwait_skip;
  u64        now;
  isize      error;
  if (eventwait) {
    error = eventwait->timed ? systimercancel(&sleepheap, eventwait->ticket) : 0;
    if (error)
      sleepfault = error;
    eventwait->result = error ? error : (flags & SIGNAL_RESTART ? 0 : -ERR_INTERRUPTED);
    eventwait->ticket = 0;
    eventwait = NULL;
    syssleepskip(trap, begin, end, resume);
    return;
  }
  if (!wait)
    return;
  error = systimernow(&now);
  if (!error && now < wait->deadline)
    settle(wait, -ERR_INTERRUPTED, wait->deadline - now);
  else
    settle(wait, error, 0);
  /* The interrupted branch may already have loaded a live ticket. Skip
     its recv so it cannot block after cancellation removed the alarm. */
  syssleepskip(trap, begin, end, resume);
  if (outer)
    syssleepskip(outer, begin, end, resume);
}

isize
systimerwait(u64 deadline, u64 events, u64 mask)
{
  Eventwait wait = {0};
  u64       ticket = 1, ignored;
  isize     error, blocked, cancelled;
  if (eventwait || pending)
    return -ERR_BUSY;
  if (sleepfault)
    return sleepfault;
  if (deadline) {
    error = systimeradd(&sleepheap, deadline, &ticket);
    if (error)
      return error;
    wait.timed = 1;
    events |= EVENT_TIMER;
  }
  wait.ticket = ticket;
  eventwait = &wait;
  error = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (!error)
    error = __sys_sleepwait(&wait.ticket, events);
  blocked = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &ignored);
  eventwait = NULL;
  cancelled = wait.ticket && wait.timed ? systimercancel(&sleepheap, ticket) : 0;
  if (cancelled)
    sleepfault = cancelled;
  if (blocked || cancelled)
    return blocked ? blocked : cancelled;
  return wait.ticket ? error : wait.result;
}

isize
sysnanosleep(u64 duration, u64 *remaining)
{
  Sleepwait *wait = NULL;
  u64        mask, ignored, now, ticket, events;
  isize      error, received = 0, restored;
  u32        i;
  if (!remaining)
    return -ERR_INVALID;
  if (!duration)
    return 0;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sleepfault ? sleepfault : systimernow(&now);
  if (!error && duration > UINT64_MAX - now)
    error = -ERR_OVERFLOW;
  if (!error) {
    for (i = 0; i < ARRAY_SIZE(waits); i++)
      if (!waits[i].used) {
        wait = &waits[i];
        break;
      }
    if (!wait || pending)
      error = -ERR_BUSY;
  }
  if (!error) {
    wait->deadline = now + duration;
    error = systimeradd(&sleepheap, wait->deadline, &ticket);
    if (!error) {
      wait->used = 1;
      wait->ticket = ticket;
      pending = wait;
    }
  }
  while (!error && wait->ticket) {
    events = EVENT_TIMER;
    if (rqprogress && rqprogress())
      events |= EVENT_RQ_DONE;
    error = systimernow(&now);
    if (!error && now >= wait->deadline)
      settle(wait, 0, 0);
    else if (!error && received)
      settle(wait, received, received == -ERR_INTERRUPTED ? wait->deadline - now : 0);
    if (error || !wait->ticket)
      break;
    error = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    if (error)
      break;
    received = __sys_sleepwait(&wait->ticket, events);
    error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &ignored);
  }
  if (wait && wait->used) {
    if (error && wait->ticket)
      settle(wait, error, 0);
    if (!error) {
      error = wait->result;
      if (error == -ERR_INTERRUPTED)
        *remaining = wait->remaining;
    }
    wait->used = 0;
  }
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : restored;
}
