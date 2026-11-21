#ifndef _FS_SEEK_H
#define _FS_SEEK_H
#include "private/file.h"
#include <sys/channel.h>

/* The dispatcher owns the admitted request and has resolved the authorized
   open File. Keep its cursor and the inode length stable through this step. */
isize fileseek(Fileinflight *active, const Ipcmsg *call, const Fileslot *slot, u64 length,
               u64 *position);
#endif
