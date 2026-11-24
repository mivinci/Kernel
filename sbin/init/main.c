/* Bounded service supervision and orphan collection for the initial image. */
#include "service.h"
#include <abi/time.h>
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
static Manifest    manifest;
extern const u8    init_config[], init_config_end[];

static NORETURN void
failed(u64 code)
{
  initstate.error = code;
  initstate.phase = 2;
  _exit((int) code);
}

static u32
reap(void)
{
  Handle pid;
  u64    kind, code, sequence;
  u32    i;
  isize  error;
  for (i = 0; i < 16; i++) {
    error = __sys_wait(-1, WAIT_WNOHANG, &pid, &kind, &code, &sequence);
    if (error == -ERR_CHILD || (!error && !pid))
      return 0;
    if (error == -ERR_INTERRUPTED)
      return 1;
    if (error)
      failed(104);
    initstate.reaped++;
    serviceexit(pid, kind, code);
  }
  return 1;
}

static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  if (__sys_clock_gettime(ABI_CLOCK_MONOTONIC, &seconds, &nanos) || seconds < 0 ||
      nanos >= UINT64_C(1000000000) || (u64) seconds > (UINT64_MAX - nanos) / UINT64_C(1000000000))
    failed(107);
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}

int
main(int argc, char **argv, char **envp)
{
  const Bootview *boot = sysboot();
  const Bootitem *own;
  Handle          pid, parent;
  u64             image, current, next, generation;
  u32             work, reapwork, phase;
  Ipcmsg          message;
  isize           error;
  (void) envp;
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
  if (manifestopen(init_config, (u64) (init_config_end - init_config), elfmachine(), &manifest))
    failed(105);
  if (serviceinit(&manifest, &package, boot, image))
    failed(106);
  for (;;) {
    reapwork = reap();
    current = now();
    if (servicestep(current, &work, &next))
      failed(108);
    phase = servicephase();
    /* FAILED is settled only after constructor/Run, wait, observation and
       parent-owned buffers have finished their independent cleanup. */
    if (phase == 3)
      failed(110);
    initstate.phase = phase == 1 ? 1 : 0;
    error = next ? __sys_alarmset(next, &generation) : __sys_alarmcancel(&generation);
    if (error)
      failed(109);
    error = ipc_recv(EVENT_CHILD | EVENT_LIFE | EVENT_TIMER | EVENT_RQ_CANCEL | EVENT_RQ_DONE,
                     work || reapwork ? RECV_NONBLOCK : 0, &message);
    if (error == -ERR_WOULD_BLOCK || error == -ERR_INTERRUPTED) {
      if (__sys_yield())
        failed(111);
      continue;
    }
    if (error)
      failed(103);
    if (message.kind == IPC_CALL && servicemessage(&message, now()))
      failed(112);
    if ((work || reapwork) && __sys_yield())
      failed(111);
  }
}
