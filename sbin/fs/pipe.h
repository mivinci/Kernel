#ifndef _FS_PIPE_H
#define _FS_PIPE_H
#include <types.h>
#include <abi/base.h>

#define PIPE_CAPACITY 8192
#define PIPE_ATOMIC   4096
#define PIPE_READ     0
#define PIPE_WRITE    1

typedef struct Pipe Pipe;
struct Pipe
{
  u32 refs[2], head, bytes;
  u8  data[PIPE_CAPACITY];
};

/* Serialized by the FS owner. Initialize fresh, prepaid storage with one
   reference per endpoint. Do not reopen an endpoint after its last drop. */
void  pipeinit(Pipe *pipe);
isize pipehold(Pipe *pipe, u32 end);
isize pipedrop(Pipe *pipe, u32 end);
/* Data buffers are private snapshots disjoint from the pipe. WOULD_BLOCK
   leaves the buffer and stream unchanged; READ returns zero at drained EOF.
   WRITE returns PEER for a broken pipe (map to EPIPE at the file boundary).
   The dispatcher checks original REQUEST ownership before committing I/O. */
isize piperead(Pipe *pipe, void *out, u32 bytes);
isize pipewrite(Pipe *pipe, const void *data, u32 bytes);
#endif
