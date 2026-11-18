/* Negative fixture: a valid image asks STATUS but deliberately never READY. */
#include "../../sbin/serial/start.h"
#include "private/start.h"
#include "private/syscall.h"
#include <proto/init.h>
#include <sys/channel.h>
int
main(int argc, char **argv)
{
  Serialboot boot;
  Ipcmsg     result;
  Msg        message;
  Handle     rpc, session;
  isize      error;
  (void) argv;
  if (argc != 7 || serialboot(sysboot(), &boot) || serialcheck(&boot) || __sys_setsid(&session))
    return 201;
  message.words[0] = INIT_WORD(INIT_STATUS);
  message.words[1] = 1;
  message.words[2] = boot.number;
  message.words[3] = boot.image;
  if (ipc_call(boot.handle[SerialInit], &message, CALL_ASYNC | CALL_CONTROL, &result) ||
      !result.request)
    return 202;
  rpc = result.request;
  for (;;) {
    error = ipc_rqpoll(rpc, 0, &result);
    if (!error)
      break;
    if (error != -ERR_WOULD_BLOCK || ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &result))
      return 203;
  }
  if (result.words[0] || result.words[1] != 1 || result.words[2] != INIT_STATE_STARTING ||
      result.words[3] || ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE))
    return 204;
  for (;;) {
    if (ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &result))
      return 205;
  }
}
