#include "private/timer.h"
#include "private/syscall.h"
#include <abi/time.h>

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
