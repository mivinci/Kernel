#ifndef _TTY_INPUT_H
#define _TTY_INPUT_H
#include "term.h"
#include <abi/signal.h>

#define TTY_INPUT_CAPACITY 8192
#define TTY_LINE_BYTES     4096
#define TTY_FLUSH_OUTPUT   1

typedef struct Ttyinput  Ttyinput;
typedef struct Ttyeffect Ttyeffect;
struct Ttyinput
{
  u64 dropped;
  u32 head, used, ready, canonical;
  /* Private tokens hold a byte and its canonical line/empty-EOF boundary.
     Uncommitted editing bytes occupy the same ring, never a copied line. */
  u16 data[TTY_INPUT_CAPACITY];
};
struct Ttyeffect
{
  u32 bytes, signal, flags, dropped;
  u8  echo[4];
};
/* Fresh private storage, before use; mode changes and live SETATTR are a
   separate service transaction. No allocation or backend references. */
isize ttyinputinit(Ttyinput *input, u32 canonical);
/* Classify a raw byte under the accepted mode, including input translation. */
u32 ttyinputsignal(const Termarg *mode, u8 byte);
/* One byte, accepted mode snapshot. All pointers are disjoint private ordinary
   storage. Success consumes the byte, including deliberately dropped overflow.
   Apply returned flush/signal and stage its echo before accepting another byte;
   never wait synchronously for a backend here. flags drops only output not yet
   submitted to that backend; input flush is already committed locally.
   echo is logical input to ttyencode, not already converted device output.
   Failure leaves input/effect unchanged. No syscall, notify or authorization. */
isize ttyinputbyte(Ttyinput *input, const Termarg *mode, u8 byte, Ttyeffect *effect);
/* Bounded dequeue to an owned service response/staging buffer, disjoint from
   input and ended; ended is also disjoint from input. Canonical reads stop at
   one boundary; empty EOF returns zero once.
   ended marks a consumed canonical boundary, even when it falls exactly at
   capacity; a larger READ must stop instead of joining the next line. Empty/
   incomplete input is WOULD_BLOCK, distinct from EOF. capacity=0 consumes
   nothing and clears ended. Raw VMIN/VTIME and foreground checks belong to the
   request service before committing this dequeue. Failure changes nothing. */
isize ttyinputread(Ttyinput *input, void *output, u32 capacity, u32 *ended);
/* Same dequeue rules, up to a full line into an owned file response page. */
isize ttyinputcopy(Ttyinput *input, volatile u8 *output, u32 capacity, u32 *ended);
/* Discard only locally unconsumed input. Preserve mode and saturated loss. */
isize ttyinputflush(Ttyinput *input);
#endif
