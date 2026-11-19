#ifndef _TTY_TERM_H
#define _TTY_TERM_H
#include <proto/tty.h>
#include <abi/base.h>

#define TTY_BATCH 32

/* Validate a private, aligned snapshot before changing live mode. speed is
   the backend's already selected rate; this function never programs it. */
isize ttyattrcheck(const Termarg *arg, u64 speed);
/* Encode a bounded input prefix into private output staging. A newline's
   CRLF is indivisible. Return output bytes and report consumed input bytes;
   a short prefix is backpressure, not an error. Pending staged output must
   drain before its source prefix can be reused or encoded again.
   Buffers and consumed are disjoint, ordinary memory owned by the service.
   Failure changes neither output nor consumed. No backend or IPC access. */
isize ttyencode(u64 oflag, const void *input, u32 bytes, void *output, u32 capacity, u32 *consumed);
#endif
