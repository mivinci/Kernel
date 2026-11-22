#ifndef _FS_FILE_H
#define _FS_FILE_H
#include "open.h"

/* A private partition of the service's opaque session numbers. */
#define FS_FILE_SESSION (UINT64_C(1) << 63)

typedef struct Fsfile  Fsfile;
typedef struct Fsref   Fsref;
typedef struct Fsfiles Fsfiles;
typedef struct Readjob Readjob;
struct Fsfile
{
  Vnode   *node;
  u64      session, position;
  u32      access, flags, refs;
  Readjob *head, *tail;
};
struct Fsref
{
  Handle  pid;
  u64     image, id;
  Fsfile *file;
};
struct Fsfiles
{
  Fsfile         *files;
  Fsref          *refs;
  u32             nfiles, nrefs;
  u64             serial;
  const Vwalkops *pins;
  void           *backend;
  /* Current length of a pinned node, without deferring the request. */
  isize (*length)(void *, const Vnode *, u64 *);
  /* Advance the original truncation. OPEN_WAIT retains all ownership;
     on cancellation, return -EINTR only after draining and establishing no
     effect. Once committed, finish with success even if cancelled. */
  isize (*truncate)(Openjob *, Vnode *, u32 cancelled);
};

/* Check the request's captured credentials against the resolved node's
   metadata before publishing an ordinary File with its own offset. */
isize fsfileoptions(const Fileslot *);
isize fsfileopen(Fsfiles *, Openjob *, Vnode *, u32 owner, u32 group, u32 mode);
isize fsfileclose(Fsfiles *, Handle, u64 image, u64 session, u64 ref);
isize fsfiledup(Fsfiles *, Handle, u64 image, u64 session, u64 ref, u64 *out);
/* Borrow while the serialized loop handles a synchronous operation. */
Fsfile *fsfilefind(Fsfiles *, Handle, u64 image, u64 session);
isize   fsfilehold(Fsfile *);
void    fsfiledrop(Fsfiles *, Fsfile *);
void    fsfileretire(Fsfiles *, Handle, u64 image);
#endif
