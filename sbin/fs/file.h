#ifndef _FS_FILE_H
#define _FS_FILE_H
#include "open.h"

/* A private partition of the service's opaque session numbers. */
#define FS_FILE_SESSION (UINT64_C(1) << 63)

typedef struct Fsfile  Fsfile;
typedef struct Fsref   Fsref;
typedef struct Fsfiles Fsfiles;
struct Fsfile
{
  Vnode *node;
  u64    session, position;
  u32    access, refs;
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
};

/* Complete an admitted and authorized ordinary OPEN. Each independent
   OPEN owns its node and offset. The table and its storage are serialized. */
isize fsfileopen(Fsfiles *, Openjob *, Vnode *, u32 access);
isize fsfileclose(Fsfiles *, Handle, u64 image, u64 session, u64 ref);
isize fsfiledup(Fsfiles *, Handle, u64 image, u64 session, u64 ref, u64 *out);
void  fsfileretire(Fsfiles *, Handle, u64 image);
#endif
