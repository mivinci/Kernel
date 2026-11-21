#ifndef _LIBSYS_FILEWATCH_H
#define _LIBSYS_FILEWATCH_H
#include "file.h"
#include <sys/channel.h>

typedef struct Filewatch Filewatch;
struct Filewatch
{
  Handle       pid;
  u64          image, connid, cookie, reason;
  Fileinflight active;
};

/* Use a zero-initialized watch in private storage separate from the region
   and descriptor output. The serialized service loop supplies a received
   CALL and a reserved unique connection ID before publishing the connection. */
isize sysfilewatch(Filewatch *watch, Handle permit, const Ipcmsg *source, u64 connid);
/* Refresh on LIFE hints. ENDED closes admission but leaves the original
   cookie and all in-flight ownership in place. */
isize sysfilewatchread(Filewatch *watch);
isize sysfilewatchaccept(Filewatch *watch, const volatile void *region, u64 bytes,
                         const Ipcmsg *source, Fileslot *out);
/* Only after retiring the connection's business references and stopping
   downstream access. A busy ledger prevents both ACK and DROP. */
isize sysfilewatchclose(Filewatch *watch);
#endif
