/* Native engine fixture; the caller's overall boot-failure policy is separate. */
#include "../../sbin/init/service.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/elf.h"
#include <abi/time.h>
#include <unistd.h>
#include <string.h>

extern const u8 init_config[], init_config_end[];
volatile u64    initserviceproof[16];
static Package  package;
static Manifest manifest;
static u8       configuration[64 + INITCFG_SERVICES * INITCFG_RECORD];

static NORETURN void
failed(u64 stage, isize error)
{
  initserviceproof[14] = stage;
  initserviceproof[15] = (u64) error;
  _exit((int) stage);
}
#define CHECK(c, n)                                                                                \
  do {                                                                                             \
    if (!(c))                                                                                      \
      failed(n, 0);                                                                                \
  } while (0)

static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  isize   error = __sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos);
  if (error || seconds < 0 || nanos >= UINT64_C(1000000000) ||
      (u64) seconds > (UINT64_MAX - nanos) / UINT64_C(1000000000))
    failed(101, error ? error : -ERR_OVERFLOW);
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}

static void
write64(u8 *data, u64 value)
{
  u32 i;
  for (i = 0; i < 8; i++)
    data[i] = (u8) (value >> (i * 8));
}
static isize
readimage(void *context, u64 offset, void *out, u64 bytes)
{
  const Bootitem *item = context;
  if (offset > item->elf_bytes || bytes > item->elf_bytes - offset)
    return -ERR_INVALID;
  memcpy(out, package.data + item->elf_off + offset, bytes);
  return 0;
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          pid, parent;
  Ipcmsg          message;
  u64   image, bytes = (u64) (init_config_end - init_config), next, generation, current, safety;
  u64   mode = 1, hash = UINT64_C(0xcbf29ce484222325), budget = 0;
  u32   i, collected = 0, killed = 0, reapwork, work, phase;
  isize error;
  CHECK(argc == 1 && !__sys_getid(&pid, &image, &parent) && pid == 1 && !parent && image == 1, 102);
  if (!strcmp(argv[0], "tests/init-timeout"))
    mode = 2;
  if (!strcmp(argv[0], "tests/init-missing"))
    mode = 3;
  if (!strcmp(argv[0], "tests/init-budget"))
    mode = 4;
  CHECK(!packageopen((const void *) boot->header->package_va, boot->header->package_bytes,
                     elfmachine(), &package) &&
            bytes <= sizeof(configuration),
        103);
  memcpy(configuration, init_config, (usize) bytes);
  if (mode == 4) {
    Elfsource       source;
    Elfplan         plan;
    const Bootitem *item = packagefind(&package, "sbin/serial");
    CHECK(item, 104);
    source.context = (void *) item;
    source.bytes = item->elf_bytes;
    source.read = readimage;
    CHECK(!elfcheck(&source, INITCFG_MAX_PAGES, &plan), 105);
    budget = plan.pages + 8;
    write64(configuration + 96, budget);
    write64(configuration + 56, 0);
    for (i = 0; i < bytes; i++) {
      hash ^= configuration[i];
      hash *= UINT64_C(0x100000001b3);
    }
    write64(configuration + 56, hash);
  }
  CHECK(!manifestopen(configuration, bytes, elfmachine(), &manifest), 106);
  CHECK(!serviceinit(&manifest, &package, boot, image), 107);
  current = now();
  CHECK(current < UINT64_MAX - UINT64_C(20000000000), 108);
  safety = current + UINT64_C(20000000000);
  for (;;) {
    reapwork = 0;
    for (i = 0; i < 16; i++) {
      Handle child;
      u64    kind, code, sequence;
      error = __sys_wait(-1, WAIT_WNOHANG, &child, &kind, &code, &sequence);
      if (error == -ERR_CHILD || (!error && !child))
        break;
      if (error)
        failed(109, error);
      collected++;
      serviceexit(child, kind, code);
      if (i == 15)
        reapwork = 1;
    }
    current = now();
    if (current >= safety)
      failed(110, -ERR_TIMEDOUT);
    error = servicestep(current, &work, &next);
    if (error)
      failed(111, error);
    phase = servicephase();
    if (mode == 1 && phase == 1 && !killed) {
      CHECK(servicestate[0].stage == 8 && servicestate[0].sequence == 1 && !servicestate[0].error &&
                servicestate[0].flags & 4,
            112);
      error = __sys_terminate(servicestate[0].control);
      if (error)
        failed(113, error);
      killed = 1;
    }
    if (phase == 3) {
      Servicestate *state = &servicestate[0];
      CHECK(state->state == SERVICE_FAILED && state->reclaimed && !state->control && !state->view &&
                !state->rings[0] && !state->rings[1] && !(state->flags & 3),
            114);
      if (mode == 1)
        CHECK(killed && state->stage == 8 && state->error == -ERR_PEER &&
                  state->exitcode == (WAIT_SIGNALLED | 9) && collected == 1,
              115);
      if (mode == 2)
        CHECK(state->stage == 7 && state->error == -ERR_TIMEDOUT && state->sequence == 1 &&
                  state->exitcode == (WAIT_SIGNALLED | 9) && collected == 1,
              116);
      if (mode == 3)
        CHECK(state->stage == 1 && state->error == -ERR_INVALID && !state->pid && !collected, 117);
      if (mode == 4)
        CHECK(state->stage == 5 && state->error == -ERR_LIMIT && state->pid && !collected &&
                  !(state->flags & 4),
              118);
      initserviceproof[0] = 1;
      initserviceproof[1] = mode;
      initserviceproof[2] = state->number;
      initserviceproof[3] = state->state;
      initserviceproof[4] = state->stage;
      initserviceproof[5] = (u64) state->error;
      initserviceproof[6] = state->exitkind;
      initserviceproof[7] = state->exitcode;
      initserviceproof[8] = state->reclaimed;
      initserviceproof[9] = collected;
      initserviceproof[10] = state->flags & 4 ? 1 : 0;
      initserviceproof[11] = budget;
      initserviceproof[12] = initaccount.data[3];
      return 42;
    }
    next = next ? MIN(next, safety) : safety;
    error = __sys_alarmset(next, &generation);
    if (error)
      failed(119, error);
    error = ipc_recv(EVENT_CHILD | EVENT_LIFE | EVENT_TIMER | EVENT_RQ_CANCEL,
                     work || reapwork ? RECV_NONBLOCK : 0, &message);
    if (error == -ERR_WOULD_BLOCK || error == -ERR_INTERRUPTED) {
      CHECK(!__sys_yield(), 120);
      continue;
    }
    if (error)
      failed(121, error);
    if (message.kind == IPC_CALL) {
      error = servicemessage(&message, now());
      if (error)
        failed(122, error);
    }
    if (work || reapwork)
      CHECK(!__sys_yield(), 123);
  }
}
