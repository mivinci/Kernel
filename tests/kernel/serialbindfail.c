/* A serial substitute for testing init's bind reply and drain paths. */
#include "../../sbin/serial/start.h"
#include "private/start.h"
#include "private/syscall.h"
#include <proto/init.h>
#include <string.h>
#include <sys/channel.h>

static int
park(void)
{
  Ipcmsg message;
  for (;;) {
    if (ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &message))
      return 208;
  }
}

int
main(int argc, char **argv)
{
  Serialboot boot;
  Ipcmsg     message;
  Msg        words;
  Handle     rpc, session;
  isize      error;
  if (argc != 7 || serialboot(sysboot(), &boot) || serialcheck(&boot) || __sys_setsid(&session))
    return 201;
  words = (Msg){{INIT_WORD(INIT_READY), 1, boot.number, boot.image}};
  if (ipc_call(boot.handle[SerialInit], &words, CALL_ASYNC | CALL_CONTROL, &message) ||
      !message.request)
    return 202;
  rpc = message.request;
  for (;;) {
    error = ipc_rqpoll(rpc, 0, &message);
    if (!error)
      break;
    if (error != -ERR_WOULD_BLOCK || ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &message))
      return 203;
  }
  if (message.words[0] || message.words[1] != 1 || message.words[2] != boot.number ||
      message.words[3] != boot.image || ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE))
    return 204;
  if (!strcmp(argv[0], "tests/serial-bind-queued"))
    return park();
  if (ipc_recv(0, 0, &message) || message.kind != IPC_CALL || message.source != 1 ||
      message.image != boot.initimage || message.words[0] != SERIAL_WORD(SERIAL_BIND) ||
      message.words[1] != 1 || handlekind((Handle) message.words[2]) != HANDLE_RESOURCE ||
      !message.words[3])
    return 205;
  if (!strcmp(argv[0], "tests/serial-bind-stall")) {
    if (ipc_defer(message.request))
      return 206;
  } else {
    words = (Msg){{(u64) -ERR_DENIED, 1, 0, 0}};
    if (ipc_reply(message.request, &words))
      return 207;
  }
  return park();
}
