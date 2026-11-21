#include <errno.h>
#include <proto/file.h>
#include <sys/channel.h>
#include "private/start.h"
#include "private/syscall.h"
#include "execclaimuser.h"

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          call = bootresource(boot, EXEC_CLAIM_CALL), rpc, pid, parent;
  Handle          space = bootresource(boot, BOOT_SELF_SPACE);
  u64             image;
  Msg             claim = {{FS_EXEC_CLAIM, 0, 1, EXEC_CLAIM_TOKEN}};
  Ipcmsg          result;
  Maparg          map = {0, 0, EXEC_CLAIM_ADDRESS, FS_CONN_BYTES, MAP_READ | MAP_WRITE, 0};
  const Filehead *head = (const Filehead *) EXEC_CLAIM_ADDRESS;
  volatile u64   *marker = (volatile u64 *) (EXEC_CLAIM_ADDRESS + FS_DATA_OFFSET);
  (void) argv;
  if (argc || __sys_getid(&pid, &image, &parent) || pid <= 1 || image != 2)
    return 71;
  if (ipc_call(call, &claim, CALL_ASYNC | CALL_RECV_MEM, &result) || !(rpc = result.request) ||
      ipc_rqwait(rpc, 0, &result) || result.kind != IPC_RESOURCE_REPLY || result.words[1] != 1 ||
      result.words[2] != EXEC_CLAIM_CONNID || result.words[3] != FS_CONN_BYTES)
    return 72;
  map.mem = (Handle) result.words[0];
  if (__sys_map(space, &map) || head->magic != FS_CONN_MAGIC || head->connid != EXEC_CLAIM_CONNID ||
      head->bytes != FS_CONN_BYTES || *marker != EXEC_CLAIM_MARKER ||
      ipc_rqwait(rpc, RQ_FINISH, &result) || ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE))
    return 73;
  *marker = ~EXEC_CLAIM_MARKER;
  claim.words[2]++;
  if (ipc_call(call, &claim, CALL_RECV_MEM, &result) || result.kind != IPC_NONE ||
      (isize) result.words[0] != -ESTALE || result.words[1] != 2 ||
      __sys_unmap(space, map.address) || __sys_free(map.mem))
    return 74;
  return 42;
}
