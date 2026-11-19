#ifndef _LIBSYS_BOOTSTRAP_H
#define _LIBSYS_BOOTSTRAP_H
#include "conn.h"
#include <sys/channel.h>
#define SYSBOOT_FREE           0
#define SYSBOOT_PREPARED       1
#define SYSBOOT_WAITING        2
#define SYSBOOT_RESULT         3
#define SYSBOOT_ACKED          4
#define SYSBOOT_RESOURCE_MOVED 1
#define SYSBOOT_BAD_RESULT     2
typedef struct Bootstrap Bootstrap;
struct Bootstrap
{
  Connclaim claim;
  Handle    rpc;
  isize     status;
  u64       kind, words[4];
  Handle    resource;
  u64       token, deadline;
  u32       opcode, state;
  u64       flags;
};
STATIC_ASSERT(sizeof(Bootstrap) == 128, bootstrap_responsibility_size);
/* Private per-backend prospective transport responsibility. No app pointer,
   File reference, CALL authority or mapping lives here. Fresh unowned storage
   only; callers exclude caught signals around short mutations. Prepare before
   native submission, then move the returned RPC before restoring the mask.
   claim/opcode/token are metadata; no service message is issued here. */
void  sysbootinit(Bootstrap *record);
isize sysbootcheck(const Bootstrap *record);
isize sysbootprepare(Bootstrap *record, Connclaim claim, u32 opcode, u64 token, u64 deadline);
isize sysbootsubmitted(Bootstrap *record, Connclaim claim, Handle *owned_rpc);
/* Protected nonblocking poll, or capture of an external unmasked wait's saved
   result after entering caller protection. No function here blocks. Raw
   query/wait errors leave WAITING; INTERRUPTED does not lose the RPC. Zero
   means a fixed result is saved, not that service/protocol succeeded. */
isize sysbootpoll(Bootstrap *record, Connclaim claim);
isize sysbootcapture(Bootstrap *record, Connclaim claim, isize status, const Ipcmsg *result);
isize sysbootcancel(Bootstrap *record, Connclaim claim);
/* ACK_RESULT|ACK_RELEASE is attempted against that same RPC. An unfinished
   request or kernel failure keeps the full record; successful ACK clears only
   RPC ownership. A resource can remain owned here after the Rq is released. */
isize sysbootack(Bootstrap *record, Connclaim claim);
isize sysbootresource(Bootstrap *record, Connclaim claim, Handle *out);
/* Caller has accepted/compensated logical token/session responsibilities.
   Neither unknown RPCs nor untransferred resources can be reset. */
isize sysbootfinish(Bootstrap *record, Connclaim claim);
/* Only before submission. This does not cancel a request or undo an effect. */
isize sysbootunprepare(Bootstrap *record, Connclaim claim);
#endif
