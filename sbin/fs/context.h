#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H
#include "walk.h"
#include <abi/base.h>

typedef struct Fscontext  Fscontext;
typedef struct Fscontexts Fscontexts;
typedef struct Fscwd      Fscwd;
typedef struct Fscwdops   Fscwdops;
struct Fscontext
{
  Handle pid;
  u64    image, version;
  Vnode *root, *cwd;
  u32    mask;
};
struct Fscontexts
{
  Fscontext      *entries;
  u32             count;
  const Vwalkops *ops;
  void           *backend;
  u64             serial;
};

/* One serialized FS image owns this fixed, initially empty table. The
   caller supplies verified process/image identities, authorized directories
   and their established namespace relationship. Each entry owns two holds,
   even when root and cwd denote the same node. */
isize fscontextadd(Fscontexts *table, Handle pid, u64 image, Vnode *root, Vnode *cwd, u32 mask);
/* Borrow until the next table mutation. Take separate node holds before
   yielding or making the result part of a pending operation. */
const Fscontext *fscontextfind(const Fscontexts *table, Handle pid, u64 image);
/* Caller has resolved cwd in this namespace and checked search permission. */
isize fscontextchdir(Fscontexts *table, Handle pid, u64 image, Vnode *cwd);
/* Replace the creation mask for the verified caller and return the old one.
   Ignore bits outside the permission mask; errors leave the entry unchanged. */
isize fscontextumask(Fscontexts *table, Handle pid, u64 image, u32 mask);
isize fscontextremove(Fscontexts *table, Handle pid, u64 image);

struct Fscwdops
{
  isize (*hold)(void *, Vnode *);
  void (*drop)(void *, Vnode *);
  /* A pure query; changes on every published directory relationship change,
     without reuse. It does not invalidate the name snapshot. */
  u64 (*version)(void *);
  /* Returns a held parent and a borrowed name span for a live directory
     entry. VWALK_WAIT must resume with the same node; cancellation drains
     before fscwddrop. */
  isize (*name)(void *, const Vnode *, Vnode **, const char **, u32 *);
};
struct Fscwd
{
  const Fscwdops *ops;
  const Vmounts  *mounts;
  void           *backend;
  Vnode          *root, *start, *node;
  u64             version, mounted, removed;
  u32             first, retries, waiting;
  char            path[FS_PATH_BYTES];
};

/* Capture the caller's root and cwd before yielding. Step returns VWALK_MORE,
   VWALK_WAIT, a negative errno, or zero with the output length including NUL.
   Output is untouched until success. */
isize fscwdinit(Fscwd *, const Vmounts *, const Fscwdops *, void *, const Fscontext *);
isize fscwdstep(Fscwd *, char *, u64, u32 *);
void  fscwddrop(Fscwd *);
#endif
