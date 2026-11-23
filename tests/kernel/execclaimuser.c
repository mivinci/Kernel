#include <errno.h>
#include <proto/file.h>
#include <sys/channel.h>
#include "private/contextipc.h"
#include "private/execfd.h"
#include "private/start.h"
#include "private/syscall.h"
#include "execclaimuser.h"

static Rbank     bank;
static Connwork  work;
static Conncache cache;
static Fdtable   fds;
static Filepool  files;

static int
claim(const Bootview *boot)
{
  Handle        ready = bootresource(boot, EXEC_CLAIM_READY);
  Handle        call = boot->fds[0].backend_call, redundant = 0, parent;
  Handle        space = bootresource(boot, BOOT_SELF_SPACE);
  Handle        memory = 0;
  Connpeer      self, peer;
  Conntag       tag;
  Connclaim     claim;
  Connentry     entry;
  Bootmap       map = {0};
  Rkey          active = {0};
  Msg           duplicate = {{FS_EXEC_CLAIM, 0, 2, boot->fds[0].prepare_token}};
  Ipcmsg        result;
  Fdview        opened;
  Fileref       reference;
  volatile u64 *marker = (volatile u64 *) (EXEC_CLAIM_ADDRESS + FS_DATA_OFFSET);
  if (__sys_getid(&self.pid, &self.image, &parent) || self.pid <= 1 || self.image != 2)
    return 71;
  if (sysctxinit(&bank, self) || sysconninit(&cache, self) || sysconnpeer(call, &peer) ||
      sysconnbind(&cache, peer, &tag) || sysconnworkinit(&work, &cache, tag) ||
      sysconnworktakecall(&work, &cache, &call, &redundant) || call || redundant)
    return 72;
  if (sysexecfdload(&fds, &files, self, boot->fds, &tag, 1) ||
      sysfdobserve(&fds, (u32) boot->fds[0].fd, &opened) || opened.tag.state != SYSFD_LIVE ||
      sysfilerefview(&files, self, opened.file, &reference))
    return 76;
  if (reference.session != boot->fds[0].session || reference.reference != boot->fds[0].reference_id)
    return 76;
  if (ready) {
    if (sysconnbegin(&cache, tag, 1, &claim) ||
        sysbootprepare(&work.state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0) ||
        sysctxhello(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) ||
        !(memory = work.state.memory) || *marker != EXEC_CLAIM_MARKER ||
        sysconnreusebegin(&cache, tag, 2, &claim) ||
        sysbootprepare(&work.state.bootstrap, claim, FS_EXEC_CLAIM, duplicate.words[3], 0))
      return 75;
  } else if (sysconnexecprepare(&work, &cache, 1, 0, duplicate.words[3], &claim, &call) ||
             call != boot->fds[0].backend_call)
    return 72;
  if (sysctxhello(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) != -ERR_STALE)
    return 72;
  if (sysctxexecclaim(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) ||
      sysconnview(&cache, tag, &entry) || entry.state != SYSCONN_READY ||
      entry.connid != EXEC_CLAIM_CONNID || work.state.map.address != EXEC_CLAIM_ADDRESS ||
      !work.state.memory || work.state.bootstrap.state != SYSBOOT_FREE || map.mapped ||
      entry.owner || active.sequence || *marker != EXEC_CLAIM_MARKER ||
      (ready && memory != work.state.memory))
    return 73;
  *marker = ~EXEC_CLAIM_MARKER;
  duplicate.words[2] = claim.seq + 1;
  if (ipc_call(work.state.call, &duplicate, CALL_RECV_MEM, &result) || result.kind != IPC_NONE ||
      (isize) result.words[0] != -ESTALE || result.words[1] != duplicate.words[2])
    return 74;
  return 42;
}

void
_start(const Boot *packet)
{
  Bootview boot;
  if (bootcheck(packet, packet->bytes, &boot) || packet->argc || packet->fd_count != 1 ||
      boot.fds[0].fd || boot.fds[0].flags || boot.fds[0].session != 100 ||
      boot.fds[0].reference_id != 1000 || boot.fds[0].prepare_token != EXEC_CLAIM_TOKEN)
    __sys_exit(70);
  __sys_exit(claim(&boot));
}
