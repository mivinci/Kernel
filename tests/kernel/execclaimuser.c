#include <errno.h>
#include <proto/file.h>
#include <sys/channel.h>
#include "private/contextipc.h"
#include "private/start.h"
#include "private/syscall.h"
#include "execclaimuser.h"

static Rbank     bank;
static Connwork  work;
static Conncache cache;

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          call = bootresource(boot, EXEC_CLAIM_CALL), redundant = 0, parent;
  Handle          space = bootresource(boot, BOOT_SELF_SPACE);
  Connpeer        self, peer;
  Conntag         tag;
  Connclaim       claim;
  Connentry       entry;
  Bootmap         map = {0};
  Rkey            active = {0};
  Msg             duplicate = {{FS_EXEC_CLAIM, 0, 2, EXEC_CLAIM_TOKEN}};
  Ipcmsg          result;
  volatile u64   *marker = (volatile u64 *) (EXEC_CLAIM_ADDRESS + FS_DATA_OFFSET);
  (void) argv;
  if (argc || __sys_getid(&self.pid, &self.image, &parent) || self.pid <= 1 || self.image != 2)
    return 71;
  if (sysctxinit(&bank, self) || sysconninit(&cache, self) || sysconnpeer(call, &peer) ||
      sysconnbind(&cache, peer, &tag) || sysconnworkinit(&work, &cache, tag) ||
      sysconnworktakecall(&work, &cache, &call, &redundant) || call || redundant ||
      sysconnbegin(&cache, tag, 1, &claim) ||
      sysbootprepare(&work.state.bootstrap, claim, FS_EXEC_CLAIM, EXEC_CLAIM_TOKEN, 0) ||
      sysctxhello(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) != -ERR_STALE)
    return 72;
  if (sysctxexecclaim(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) ||
      sysconnview(&cache, tag, &entry) || entry.state != SYSCONN_READY ||
      entry.connid != EXEC_CLAIM_CONNID || work.state.map.address != EXEC_CLAIM_ADDRESS ||
      !work.state.memory || work.state.bootstrap.state != SYSBOOT_FREE || map.mapped ||
      active.sequence || *marker != EXEC_CLAIM_MARKER)
    return 73;
  *marker = ~EXEC_CLAIM_MARKER;
  if (ipc_call(work.state.call, &duplicate, CALL_RECV_MEM, &result) || result.kind != IPC_NONE ||
      (isize) result.words[0] != -ESTALE || result.words[1] != 2)
    return 74;
  return 42;
}
