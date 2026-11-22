#ifndef _FS_ACCESS_H
#define _FS_ACCESS_H
#include <types.h>
#include <sys/channel.h>

typedef struct Vcred Vcred;
struct Vcred
{
  u32 uid, gid;
};

#define VACCESS_X 1U
#define VACCESS_W 2U
#define VACCESS_R 4U

/* Accept only a received CALL. The kernel resolves its original source.
   Use effective credentials for OPEN and real credentials for access(). */
isize vcredsource(const Ipcmsg *call, u32 real, Vcred *out);
isize vaccess(const Vcred *cred, u32 owner, u32 group, u32 mode, u32 requested, u32 directory);
#endif
