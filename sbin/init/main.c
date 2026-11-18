/* Initial userspace catalog and orphan-reaping loop. Service launch follows
   separately; reaching this loop is not a service READY handshake. */
#include "package.h"
#include <sys/channel.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include "private/start.h"
#include "private/elf.h"

typedef struct Initstate Initstate;
struct Initstate
{
  u64 phase, pid, image, files, bytes, checksum, reaped, error;
};
/* Private process state, useful to STATUS implementation and debugger alike.
   Not a wire ABI or authority source. */
volatile Initstate initstate;
static Package     package;

static NORETURN void
failed(u64 code)
{
  initstate.error = code;
  initstate.phase = 2;
  _exit((int) code);
}

static void
reap(void)
{
  pid_t pid;
  for (;;) {
    pid = waitpid(-1, NULL, WNOHANG);
    if (pid > 0) {
      initstate.reaped++;
      continue;
    }
    if (!pid || errno == ECHILD)
      return;
    if (errno != EINTR)
      failed(104);
  }
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  const Bootitem *own;
  Handle          pid, parent;
  u64             image;
  Ipcmsg          message;
  isize           error;
  if (__sys_getid(&pid, &image, &parent) || pid != 1 || parent || !image)
    failed(100);
  initstate.pid = (u64) pid;
  initstate.image = image;
  if (!boot || !boot->header || !boot->header->package_bytes ||
      !bootresource(boot, BOOT_SELF_SPACE) || !bootresource(boot, BOOT_SELF_ALLOC) ||
      packageopen((const void *) boot->header->package_va, boot->header->package_bytes,
                  elfmachine(), &package))
    failed(101);
  own = argc == 1 ? packagefind(&package, argv[0]) : NULL;
  if (own != &package.files[package.header.init_index])
    failed(102);
  initstate.files = package.header.count;
  initstate.bytes = package.header.bytes;
  initstate.checksum = package.header.checksum;
  for (;;) {
    reap();
    initstate.phase = 1;
    error = ipc_recv(EVENT_CHILD, RECV_EVENTS_ONLY, &message);
    if (error == -ERR_INTERRUPTED)
      continue;
    if (error || message.kind != IPC_EVENT || message.source || message.request ||
        !(message.words[0] & EVENT_CHILD))
      failed(103);
  }
}
