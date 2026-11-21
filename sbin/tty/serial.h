#ifndef _TTY_SERIAL_H
#define _TTY_SERIAL_H
#include "input.h"
#include <proto/serial.h>

typedef struct Ttyserial       Ttyserial;
typedef struct Ttyserialresult Ttyserialresult;
struct Ttyserial
{
  Serialring *rx, *tx;
  u64         epoch;
  u32         queued;
  u8          output[2 * TTY_BATCH + 8];
};
struct Ttyserialresult
{
  u64 hints, end;
  u32 again, signal;
};
/* Borrow distinct coherent rings for the lifetime of this attachment. */
isize ttyserialinit(Ttyserial *io, Serialring *rx, Serialring *tx, u64 epoch);
/* Accept a bounded logical output prefix into private staging. Returns the
   logical bytes accepted, or zero if the previous batch still owns staging. */
isize ttyserialoutput(Ttyserial *io, u64 oflag, const void *data, u32 size);
/* Process up to rxlimit RX bytes and one TX batch. A timed reader can check
   its deadline and retention after each byte by passing rxlimit=1.
   Notify with hints even after partial progress followed by failure.
   Dispatch signal before the next step.
   end reports RX flags after all preceding ring data has reached input.
   Yield and recheck when again is set, without waiting for another hint. */
isize ttyserialstep(Ttyserial *io, Ttyinput *input, const Termarg *mode, u32 rxlimit,
                    Ttyserialresult *out);
#endif
