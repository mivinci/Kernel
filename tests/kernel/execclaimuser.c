#include <errno.h>
#include <proto/file.h>
#include <sys/channel.h>
#include "private/contextipc.h"
#include "private/fileimage.h"
#include "private/start.h"
#include "private/syscall.h"
#include "execclaimuser.h"

static Rbank      bank;
static Fileimage *state;
#define work  state->connections[0]
#define cache (*sysfileimagecache(state))

static int
claim(const Bootview *boot)
{
  Handle        ready = bootresource(boot, EXEC_CLAIM_READY);
  Handle        parent;
  Handle        space = bootresource(boot, BOOT_SELF_SPACE);
  Handle        memory = 0;
  Connpeer      self;
  Conntag       tag;
  Connclaim     claim;
  Connentry     entry;
  Bootmap       map = {0};
  Rkey          active = {0};
  Msg           duplicate = {{FS_EXEC_CLAIM, 0, 2, boot->fds[0].prepare_token}};
  Ipcmsg        result;
  Fdview        opened;
  Fileref       reference;
  Fileioresult  io;
  u64           received;
  volatile u64 *marker = (volatile u64 *) (EXEC_CLAIM_ADDRESS + FS_DATA_OFFSET);
  if (__sys_getid(&self.pid, &self.image, &parent) || self.pid <= 1 || self.image != 2)
    return 71;
  if (sysctxinit(&bank, self))
    return 72;
  tag = work.state.tag;
  if (sysfdobserve(&state->fds, (u32) boot->fds[0].fd, &opened) || opened.tag.state != SYSFD_LIVE ||
      sysfilerefview(&state->files, self, opened.file, &reference) || reference.aliases != 1)
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
    if (sysctxhello(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active) !=
            -ERR_STALE ||
        sysctxexecclaim(&bank, 0, &work, &cache, space, EXEC_CLAIM_ADDRESS, &map, &active))
      return 72;
  } else if (sysfileclaimrange(EXEC_CLAIM_ADDRESS) ||
             sysfileimageio(0, (int) opened.tag.index, FS_READ, NULL, &received, sizeof(received),
                            &io) ||
             io.native || io.business || io.count != sizeof(received) ||
             received != EXEC_CLAIM_MARKER)
    return 72;
  if (sysfileclaimfd(0, (int) opened.tag.index, EXEC_CLAIM_ADDRESS) ||
      sysconnview(&cache, tag, &entry) || entry.state != SYSCONN_READY ||
      entry.connid != EXEC_CLAIM_CONNID || work.state.map.address != EXEC_CLAIM_ADDRESS ||
      !work.state.memory || work.state.bootstrap.state != SYSBOOT_FREE || map.mapped ||
      entry.owner || active.sequence || *marker != EXEC_CLAIM_MARKER ||
      (ready && memory != work.state.memory))
    return 73;
  *marker = ~EXEC_CLAIM_MARKER;
  duplicate.words[2] = entry.sequence + 1;
  if (ipc_call(work.state.call, &duplicate, CALL_RECV_MEM, &result) || result.kind != IPC_NONE ||
      (isize) result.words[0] != -ESTALE || result.words[1] != duplicate.words[2])
    return 74;
  return 42;
}

void
_start(const Boot *packet)
{
  const Bootview *boot = sysboot();
  int             argc;
  char          **argv, **envp;
  if (sysstartargswithfds(packet, &argc, &argv, &envp, sysfilebootinstall) || argc || argv[0] ||
      envp[0] || packet->fd_count != 1 || boot->fds[0].fd || boot->fds[0].flags ||
      boot->fds[0].session != 100 || boot->fds[0].reference_id != 1000 ||
      boot->fds[0].prepare_token != EXEC_CLAIM_TOKEN || sysboot() != boot || sysfileimage(&state))
    __sys_exit(70);
  __sys_exit(claim(boot));
}
