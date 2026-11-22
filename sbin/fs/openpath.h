#ifndef _FS_OPENPATH_H
#define _FS_OPENPATH_H
#include "access.h"
#include "walk.h"
#include "private/filewatch.h"

/* Start the lookup of an already admitted OPEN. The caller supplies the
   trusted root and relative start (including directory-session validation),
   chooses final-link behavior and handles OPEN flags/mode and publication.
   Search callbacks can use cred after this returns successfully. On all
   returns, the caller still owns the REQUEST and its slot. */
isize vopenpath(Vwalk *walk, const Vmounts *mounts, const Vwalkops *ops, void *context, Vnode *root,
                Vnode *start, u32 follow, Vcred *cred, const Filelink *link, const Ipcmsg *call,
                const Fileslot *slot);
#endif
