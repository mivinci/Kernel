#ifndef _FS_OPEN_H
#define _FS_OPEN_H
#include "context.h"
#include "openpath.h"

typedef struct Fsopen  Fsopen;
typedef struct Openjob Openjob;
struct Openjob
{
  Vwalk     walk;
  Ipcmsg    call;
  Fileslot  slot;
  Filelink *link;
  void     *backend;
  Vcred     cred;
  u32       mask, waiting, changedcwd;
};
struct Fsopen
{
  Fscontexts     *contexts;
  const Vmounts  *mounts;
  const Vwalkops *ops; /* Walk callbacks receive Openjob *. */
  Openjob        *jobs;
  u32             count;
  u64             events;
  void           *backend;
  /* Validate flags/mode and any directory session before lookup. Return
     negative business errno or fill the borrowed start and final-link rule. */
  isize (*select)(Openjob *, const Fscontext *, Vnode **start, u32 *follow);
  /* Drain a waiting backend step without dropping its node references.
     WOULD_BLOCK keeps the whole job; backend progress must wake the loop. */
  isize (*cancel)(Openjob *);
  /* Finish the original REQUEST, including business errors. The node is
     borrowed during this call. Zero means the native request was finished;
     the dispatcher releases the claimed slot. A native failure must leave
     the request unconsumed and no publication outstanding. */
  isize (*complete)(Openjob *, Vnode *);
};

isize fsopenstart(Fsopen *, Filelink *, u32, const Ipcmsg *);
isize fsopenpump(Fsopen *);
int   fsopenbusy(const Fsopen *, const Filelink *);
#endif
