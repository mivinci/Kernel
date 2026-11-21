#ifndef _LIBSYS_FILEIMAGE_H
#define _LIBSYS_FILEIMAGE_H
#include "contextipc.h"
#include "fd.h"
#include "filecreate.h"

typedef struct Fileimage Fileimage;
struct Fileimage
{
  Connwork connections[SYSCONN_COUNT];
  Fdtable  fds;
  Filepool files;
  Rbank    contexts;
};

/* One shared file state per image, initialized on demand. Inherited image
   repair must be wired before these records can be used by a fork child. */
isize      sysfileimage(Fileimage **out);
Conncache *sysfileimagecache(Fileimage *image);
/* Resolve the fixed Boot FS endpoint and continue its initial HELLO. The
   caller provides a reserved mapping address for the cold path. A failed
   map with no alias permits retry at another address. A ready connection
   ignores the proposed address; all callers use the same table and backend. */
isize sysfileimagefs(u32 layer, u64 address, Connwork **out);
/* Drive PIPE on the established FS connection. The caller retains the
   continuation; failed copyout hands both File references to the image worker. */
isize sysfileimagepipe(u32 layer, Connwork *work, Pipecreate *pipe, Rkey *active, int *fds,
                       isize *business);
/* Duplicate a live descriptor in the current image without a service RPC. */
isize sysfileimagedup(int fd);
isize sysfileimagedup2(int fd, int target);
/* ERR_INTERRUPTED means the fd was detached and cleanup still owns the File. */
isize sysfileimageclose(int fd);
#endif
