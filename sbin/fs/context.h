#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H
#include "walk.h"
#include <abi/base.h>

typedef struct Fscontext  Fscontext;
typedef struct Fscontexts Fscontexts;
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
isize fscontextremove(Fscontexts *table, Handle pid, u64 image);
#endif
