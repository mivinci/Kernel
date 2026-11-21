#ifndef _FS_PIPEFILE_H
#define _FS_PIPEFILE_H
#include "pipe.h"

typedef struct Pipefile  Pipefile;
typedef struct Piperef   Piperef;
typedef struct Pipetable Pipetable;
typedef struct Pipekey   Pipekey;
typedef struct Pipeready Pipeready;
struct Pipefile
{
  Pipe stream;
  u64  session[2];
  u32  nonblock[2];
};
struct Piperef
{
  Handle pid;
  u64    image, session, reference;
};
struct Pipetable
{
  Pipefile *files;
  Piperef  *refs;
  u32       nfiles, nrefs;
  u64       serial;
};
struct Pipekey
{
  u64 session, reference;
};
struct Pipeready
{
  u32 readable, room, hup, error;
};

/* Serialized service state backed by fixed zeroed storage. Supply the true
   caller identity only after connection admission. IDs are never reused. */
isize pipefilecreate(Pipetable *table, Handle pid, u64 image, u32 nonblock, Pipekey keys[2]);
isize pipefiledup(Pipetable *table, Handle pid, u64 image, Pipekey source, u64 *reference);
isize pipefileclose(Pipetable *table, Handle pid, u64 image, Pipekey key);
isize pipefileretire(Pipetable *table, Handle pid, u64 image);
/* An instantaneous readiness snapshot of the caller's authorized endpoint.
   No durable pin, subscription or reservation is created. */
isize pipefileready(Pipetable *table, Handle pid, u64 image, u64 session, Pipeready *out);
isize pipefilecheck(Pipetable *table, Handle pid, u64 image, u64 session);
/* An admitted I/O holds one endpoint reference through reply/drain, even if
   the client's last business reference disappears meanwhile. */
isize pipefilepin(Pipetable *table, Handle pid, u64 image, u64 session, u32 end, Pipefile **out);
#endif
