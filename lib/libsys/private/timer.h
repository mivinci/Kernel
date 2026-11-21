#ifndef _LIBSYS_TIMER_H
#define _LIBSYS_TIMER_H
#include <types.h>
#include <abi/base.h>

#define SYSTIMER_LIMIT 64

typedef struct Timerjob  Timerjob;
typedef struct Timerheap Timerheap;
struct Timerjob
{
  u64 deadline, ticket;
};
struct Timerheap
{
  Timerjob jobs[SYSTIMER_LIMIT];
  u64      serial, generation;
  u32      count;
};

/* One zero-initialized heap owns the Image's native alarm. Exclude caught
   signals during updates. Out parameters live outside the heap. */
isize systimernow(u64 *out);
isize systimeradd(Timerheap *heap, u64 deadline, u64 *ticket);
isize systimercancel(Timerheap *heap, u64 ticket);
/* Use actual monotonic time, never infer expiry from a notification.
   Compare TIMER words[1] with generation when dispatching hints. Drain all
   due jobs before waiting again, including jobs with the same deadline. */
isize systimerdue(Timerheap *heap, u64 now, Timerjob *out);

isize sysnanosleep(u64 duration, u64 *remaining);
int   syssleepwaiting(void);
void  syssleepinterrupt(void *trap, void *outer);
/* Architecture guard between the last ticket load and the blocking recv. */
isize __sys_sleepwait(const volatile u64 *ticket);
void  syssleepskip(void *trap, u64 begin, u64 end, u64 resume);
#endif
