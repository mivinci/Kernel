#ifndef _TTY_SERIAL_H
#define _TTY_SERIAL_H
#include "input.h"
#include "wait.h"
#include <proto/serial.h>
#include <proto/file.h>
#include "private/file.h"
#include <sys/channel.h>

typedef struct Ttyserial       Ttyserial;
typedef struct Ttyserialresult Ttyserialresult;
typedef struct Ttypending      Ttypending;
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
struct Ttypending
{
  Ipcmsg       call;
  Fileslot     slot;
  volatile u8 *page;
  Ttywait      wait;
  u32          timed;
};
/* Borrow distinct coherent rings for the lifetime of this attachment. */
isize ttyserialinit(Ttyserial *io, Serialring *rx, Serialring *tx, u64 epoch);
/* Accept a bounded logical output prefix into private staging. Returns the
   logical bytes accepted, or zero if the previous batch still owns staging. */
isize ttyserialoutput(Ttyserial *io, u64 oflag, const void *data, u32 size);
/* Commit one prefix of an authorized WRITE from its submitted data page.
   The service checks reference, foreground and request cancellation first.
   A positive result counts input bytes; WOULD_BLOCK commits nothing. */
isize ttyserialwrite(Ttyserial *io, u64 oflag, const Fileslot *slot, const volatile u8 *page);
/* Commit a READ after authorization, readiness and cancellation checks. */
isize ttyserialread(Ttyinput *input, const Fileslot *slot, volatile u8 *page);
/* The service has authorized this request and owns the exact slot. On
   WOULD_BLOCK it retains that ownership for a later readiness check. */
isize ttyfileio(Ttyserial *io, Ttyinput *input, u64 oflag, Fileinflight *active, const Ipcmsg *call,
                const Fileslot *slot, volatile u8 *page, u32 nonblock);
/* Reply to an authorized TTYCTL using the exact admitted REQUEST and slot.
   GETATTR publishes the current private mode in the original data page. */
isize ttyfilectl(const Termarg *mode, Fileinflight *active, const Ipcmsg *call,
                 const Fileslot *slot, volatile u8 *page);
/* After blocking I/O returned WOULD_BLOCK, snapshot the original request
   and optional READ wait state. Initialize pending to zero and keep the
   ledger and mapping alive until resume settles it. For a timed READ pass
   current monotonic time and a separate alarm output to resume. */
isize ttyfiledefer(Ttypending *pending, const Fileinflight *active, const Ipcmsg *call,
                   const Fileslot *slot, volatile u8 *page, const Ttywait *wait);
isize ttyfileresume(Ttypending *pending, Ttyserial *io, Ttyinput *input, u64 oflag,
                    Fileinflight *active, u64 now, u64 *next);
/* Use the wait snapshot taken at admission. While blocked, next is the
   absolute alarm deadline, or zero for an untimed wait. */
isize ttyfilereadwait(Ttyinput *input, Fileinflight *active, const Ipcmsg *call,
                      const Fileslot *slot, volatile u8 *page, Ttywait *wait, u64 now, u64 *next);
/* Process up to rxlimit RX bytes and one TX batch. A timed reader can check
   its deadline and retention after each byte by passing rxlimit=1.
   Notify with hints even after partial progress followed by failure.
   Dispatch signal before the next step.
   end reports RX flags after all preceding ring data has reached input.
   Yield and recheck when again is set, without waiting for another hint. */
isize ttyserialstep(Ttyserial *io, Ttyinput *input, const Termarg *mode, u32 rxlimit,
                    Ttyserialresult *out);
/* For one blocking interbyte READ, return 1 when it can complete. A ready
   read takes precedence over the next RX byte. Otherwise consume at most
   one byte and restart the deadline only if that byte was retained. */
isize ttyserialtimedstep(Ttyserial *io, Ttyinput *input, const Termarg *mode, Ttywait *wait,
                         u64 now, u64 *next, Ttyserialresult *out);
/* Advance a deferred interbyte READ. Settle a ready/cancelled request before
   consuming RX, otherwise process at most one byte. WOULD_BLOCK preserves
   the request and returns the next deadline and any ring/signal effects. */
isize ttyfileinterbyte(Ttypending *pending, Ttyserial *io, Ttyinput *input, const Termarg *mode,
                       Fileinflight *active, u64 now, u64 *next, Ttyserialresult *out);
/* Run a step and publish its hints on the exact serial peer's NOTIFY handle. */
isize ttyserialpump(Ttyserial *io, Ttyinput *input, const Termarg *mode, u32 rxlimit, Handle notify,
                    Ttyserialresult *out);
#endif
