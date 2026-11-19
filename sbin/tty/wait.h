#ifndef _TTY_WAIT_H
#define _TTY_WAIT_H
#include "input.h"

#define TTY_READ_BYTES 4096

typedef struct Ttywait Ttywait;
struct Ttywait
{
  u64 deadline, last;
  u32 length, minimum, time, canonical, nonblock, started;
};
/* Disjoint private ordinary state, accepted mode and current input.ready count.
   now is monotonic ns; existing raw input starts an interbyte timer at acceptance.
   Failure leaves output intact. No input consumption or timer syscall. */
isize ttywaitinit(Ttywait *wait, const Termarg *mode, u32 length, u32 nonblock, u32 available,
                  u64 now);
/* Check before committing another input byte to this request. 1 means its old
   deadline is already due: settle that read against the old queue first. 0
   records a retained byte and, only for MIN>0/TIME>0, restarts the interval.
   Total-read deadlines never move. Rejected/overflow input must not call this.
   Failure and a due deadline leave the request unchanged. */
isize ttywaitbyte(Ttywait *wait, u64 now);
/* 1 can complete, 0 waits, WOULD_BLOCK is a completed nonblocking empty read.
   next is the absolute alarm deadline only while waiting, otherwise zero.
   This is not a reservation: foreground/cancellation and queue-to-response
   commit are still required. Check due deadlines before processing new input.
   Invalid state/time leaves both outputs unchanged. */
isize ttywaitcheck(Ttywait *wait, u32 available, u64 now, u64 *next);
#endif
