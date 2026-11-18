#ifndef _SERIAL_CONTROL_H
#define _SERIAL_CONTROL_H
#include "io.h"
#include <sys/channel.h>

typedef struct Serialctl Serialctl;
struct Serialctl
{
  Handle pid, peer, peerpid;
  u64    image, initimage, peerimage, sequence;
  Msg    request, reply;
};
/* Once before the service runs; never reset a live binding/sequence. */
isize serialctlinit(Serialctl *control, Handle pid, u64 image, u64 initimage);
/* One accepted CALL with valid private input/output. Fills the four business
   reply words, does not reply/finish the kernel REQUEST. io is live if online.
   Recognized authorized commands retain their last result, including failure;
   an exact duplicate has no further query/device/notification side effects.
   The controller owns cleanup of installed resources after a rejected BIND. */
void serialcontrol(Serialctl *control, Serialio *io, const Ipcmsg *message, u32 online,
                   u32 cancelled, Msg *reply);
#endif
