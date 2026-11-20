/* Finite management model; actual kernel/ELF/IRQ execution is tested in QEMU. */
#include "../../sbin/init/service.h"
#include "private/syscall.h"
#include "private/elf.h"
#include <abi/resource.h>
#include <abi/system.h>
#include <abi/observe.h>
#include <proto/init.h>
#include <proto/serial.h>
#include <abi/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define H(n)  ((Handle) ((UINT64_C(2) << 59) | (UINT64_C(1) << 16) | (n)))
#define CHILD ((Handle) ((UINT64_C(1) << 59) | (UINT64_C(1) << 16) | 1))
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      fprintf(stderr, "manager check line %d\n", __LINE__);                                        \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
static const char *scenario;
static u32         created, published, ended, alive, maps, allocated, freed, destroyed, terminated;
static u32         observed, dropped, replied, finished, rqstate = RQ_CHECK_LIVE, handoffs;
static u64         image = 1;
static Msg         result;
static u64         timestamp;
static u64         signalmask;

isize
__sys_clock_gettime(u64 clock, int64_t *seconds, u64 *nanos)
{
  CHECK(clock == CLOCK_MONOTONIC);
  *seconds = (int64_t) (timestamp / UINT64_C(1000000000));
  *nanos = timestamp % UINT64_C(1000000000);
  return 0;
}

isize
__sys_getid(Handle *pid, u64 *number, Handle *parent)
{
  *pid = 1;
  *number = 1;
  *parent = 0;
  return 0;
}
isize
__sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *rows, u64 capacity, u64 *written,
            u64 *next)
{
  (void) cursor;
  CHECK(capacity);
  memset(rows, 0, (usize) capacity * sizeof(*rows));
  *written = 1;
  *next = 0;
  rows[0].type = kind;
  rows[0].pid = authority ? CHILD : 1;
  rows[0].image = 1;
  rows[0].stamp = 1;
  if (kind == QUERY_RESOURCE) {
    CHECK(!authority && capacity >= 2);
    *written = 2;
    rows[0].data[0] = H(18);
    rows[0].data[1] = RESOURCE_CALL;
    rows[0].data[2] = RESOURCE_USE | RESOURCE_GRANT;
    rows[0].data[6] = rows[0].data[7] = 1;
    rows[1].type = kind;
    rows[1].pid = rows[1].image = 1;
    rows[1].data[0] = H(22);
    rows[1].data[1] = RESOURCE_OBSERVE;
    rows[1].data[2] = OBSERVE_MONITOR;
  } else if (kind == QUERY_BOOT_RESOURCE) {
    CHECK(authority == H(7));
    rows[0].pid = 1;
    rows[0].data[0] = H(20);
    rows[0].data[1] = BOOT_UART_NS16550;
    rows[0].data[2] = 0x10000000;
    rows[0].data[3] = 8;
    rows[0].data[4] = H(21);
    rows[0].data[7] = 1;
  } else if (kind == QUERY_PROC) {
    CHECK(authority == H(24));
    if (!alive)
      *written = 0;
    rows[0].image = image;
    rows[0].data[3] = ended ? PROCESS_EXITING : PROCESS_BLOCKED;
  } else {
    CHECK(kind == QUERY_ACCT);
    rows[0].image = 0;
    rows[0].data[1] = authority ? 64 : 4096;
  }
  return 0;
}
isize
__sys_debughandoff(Handle handle)
{
  CHECK(handle == H(7));
  handoffs++;
  return !strcmp(scenario, "handoff") && handoffs == 1 ? -ERR_BUSY : 0;
}
isize
__sys_create(Handle handle, const Createarg *args, Handle *view, Handle *pid, Handle *control)
{
  CHECK(handle == H(3) && args->memlimit == 64 && args->exec_limit == 128 &&
        args->resources == 16 && args->pending == 4);
  if (!strcmp(scenario, "create")) {
    *view = *pid = *control = 0;
    return -ERR_NOMEM;
  }
  CHECK(!created);
  created = alive = 1;
  *view = H(23);
  *pid = CHILD;
  *control = H(24);
  return 0;
}
isize
__sys_sigmask(u64 how, u64 mask, u64 *old)
{
  u64 saved = signalmask;
  CHECK(how == SIGNAL_BLOCK || how == SIGNAL_SETMASK);
  signalmask = how == SIGNAL_BLOCK ? signalmask | mask : mask;
  *old = saved;
  return 0;
}
isize
__sys_copy(void *out, const void *in, u64 bytes)
{
  CHECK(signalmask == SIGNAL_SUPPORTED);
  if (!strcmp(scenario, "args"))
    return -ERR_FAULT;
  memcpy(out, in, bytes);
  return 0;
}
isize
__sys_alloc(Handle handle, const Allocarg *args, Handle *out)
{
  CHECK(handle == H(2) && args->kind == ALLOC_SHARED && args->bytes == SERIAL_RING_BYTES &&
        allocated < 2);
  *out = H(25 + allocated);
  allocated++;
  return 0;
}
isize
__sys_map(Handle handle, const Maparg *args)
{
  void *mapped;
  u64   slot = args->address == SERVICE_RING_LO ? 0 : 1;
  CHECK(handle == H(1) && args->address == SERVICE_RING_LO + slot * SERVICE_TX_OFF &&
        args->length == SERIAL_RING_BYTES && args->mem == H(25 + slot) && args->prot == 3 &&
        !args->offset);
  if (!strcmp(scenario, "map") && slot)
    return -ERR_NOMEM;
  mapped = mmap((void *) args->address, SERIAL_RING_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
  CHECK(mapped == (void *) args->address);
  maps |= 1U << slot;
  return 0;
}
isize
__sys_unmap(Handle handle, u64 address)
{
  u32 slot = address == SERVICE_RING_LO ? 0 : 1;
  CHECK(handle == H(1) && !alive && (maps & (1U << slot)));
  CHECK(!munmap((void *) address, SERIAL_RING_BYTES));
  maps &= ~(1U << slot);
  return 0;
}
isize
__sys_grant(Handle view, Handle handle, u64 rights, u64 offset, u64 bytes, u64 inherit, Handle *out)
{
  static u32 grants;
  CHECK(view == H(23) && !offset && !inherit && !published);
  CHECK(handle == H(18) || handle == H(20) || handle == H(25) || handle == H(26));
  CHECK(rights == RESOURCE_USE || rights == 3);
  (void) bytes;
  grants++;
  if (!strcmp(scenario, "grant") && grants == 3)
    return -ERR_DENIED;
  *out = (Handle) ((UINT64_C(2) << 59) | (UINT64_C(7) << 16) | grants);
  return 0;
}
isize
__sys_irqbind(Handle control, Handle source, u32 bit, Handle *out)
{
  CHECK(control == H(24) && source == H(21) && bit == SERIAL_IRQ_BIT);
  *out = H(30);
  return 0;
}
u32
elfmachine(void)
{
  return 243;
}
u32
elfentryalign(void)
{
  return 2;
}
isize
elfload(const Elfsource *source, const Elfplan *plan, const void *data, u64 bytes, Elfwork *work,
        Config *out)
{
  const Boot   *boot = data;
  const Strref *argv = (const Strref *) ((const u8 *) data + boot->argv_off);
  const char   *expected[7] = {"sbin/serial", "ns16550", "0", "8", "1", "0", "1"};
  u32           i;
  CHECK(!signalmask && bytes == boot->bytes && bytes < 568 && !boot->envc && !boot->env_off);
  CHECK(boot->argv_off >= sizeof(*boot) && boot->argv_off + 7 * sizeof(Strref) <= bytes);
  for (i = 0; i < 7; i++) {
    CHECK(argv[i].offset <= bytes && argv[i].length <= bytes - argv[i].offset);
    CHECK(argv[i].length == strlen(expected[i]) + 1);
    CHECK(!memcmp((const u8 *) data + argv[i].offset, expected[i], argv[i].length));
  }
  CHECK(source->bytes >= 64 && plan->entry == 0x10000 && boot->argc == 7 &&
        boot->resource_count == 7 && work->view == H(23) && work->parent_space == H(1) &&
        !work->memory && !work->mapped);
  if (!strcmp(scenario, "load"))
    return -ERR_NOMEM;
  memset(out, 0, sizeof(*out));
  out->entry = plan->entry;
  return 0;
}
isize
elfclean(Elfwork *work)
{
  CHECK(!work->memory && !work->mapped);
  return 0;
}
isize
__sys_configure(Handle view, const Config *config, u64 reserve)
{
  CHECK(view == H(23) && config->entry == 0x10000 && !reserve);
  return 0;
}
isize
__sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 number, u64 *cookie,
              u64 *state, u64 *reason)
{
  CHECK(permit == H(22) && basis == H(24) && relation == OBS_MONITOR && pid == CHILD &&
        number == 1 && !observed);
  observed = 1;
  *cookie = 501;
  *state = OBS_PREPARING;
  *reason = 0;
  return 0;
}
isize
__sys_obsread(u64 cookie, u64 *state, u64 *reason, Handle *pid, u64 *number)
{
  CHECK(cookie == 501 && observed);
  *state = ended ? OBS_ENDED : published ? OBS_ACTIVE : OBS_PREPARING;
  *reason = OBS_EXIT;
  *pid = CHILD;
  *number = 1;
  return 0;
}
isize
__sys_obsack(u64 cookie)
{
  CHECK(cookie == 501 && observed && ended);
  observed = 0;
  dropped++;
  return 0;
}
isize
__sys_obsdrop(u64 cookie)
{
  CHECK(cookie == 501 && observed);
  observed = 0;
  dropped++;
  return 0;
}
isize
__sys_start(Handle view)
{
  CHECK(view == H(23) && observed && !published);
  published = 1;
  return 0;
}
isize
__sys_destroy(Handle view)
{
  CHECK(view == H(23) && !published && !observed);
  destroyed++;
  alive = 0;
  ended = 1;
  return 0;
}
isize
__sys_terminate(Handle control)
{
  CHECK(control == H(24) && published);
  terminated++;
  ended = 1;
  return 0;
}
isize
__sys_free(Handle handle)
{
  CHECK(!alive);
  if (handle == H(24))
    CHECK(created);
  else
    CHECK(handle == H(25) || handle == H(26));
  freed++;
  return 0;
}
isize
ipc_rqcheck(Handle request, u64 *state)
{
  CHECK(request == 3);
  *state = rqstate;
  return 0;
}
isize
ipc_rqfinish(Handle request)
{
  CHECK(request == 3 && rqstate == RQ_CHECK_DRAINING);
  finished++;
  return 0;
}
isize
ipc_reply(Handle request, const Msg *words)
{
  CHECK(request == 3);
  result = *words;
  replied++;
  return 0;
}

static u32
step(u64 now, u64 *next)
{
  u32 work;
  timestamp = now;
  CHECK(!servicestep(now, &work, next));
  return work;
}
static void
request(u64 opcode, u64 sequence, Handle source, u64 number)
{
  Ipcmsg message;
  memset(&message, 0, sizeof(message));
  message.kind = IPC_CALL;
  message.source = source;
  message.request = 3;
  message.image = number;
  message.words[0] = INIT_WORD(opcode);
  message.words[1] = sequence;
  message.words[2] = 1;
  message.words[3] = number;
  CHECK(!servicemessage(&message, 10000000));
}
int
main(int argc, char **argv)
{
  FILE    *file;
  long     bytes;
  u8      *data;
  Manifest manifest;
  Package  package;
  struct
  {
    Boot     header;
    Resentry entries[4];
  } packet;
  Bootview boot;
  u64      next = 0, now = 0;
  u32      i;
  CHECK(argc == 4);
  scenario = argv[1];
  file = fopen(argv[2], "rb");
  CHECK(file);
  CHECK(!fseek(file, 0, SEEK_END));
  bytes = ftell(file);
  CHECK(bytes > 0 && !fseek(file, 0, SEEK_SET));
  data = malloc((size_t) bytes);
  CHECK(data && fread(data, 1, (size_t) bytes, file) == (size_t) bytes);
  fclose(file);
  CHECK(!packageopen(data, (u64) bytes, 243, &package));
  file = fopen(argv[3], "rb");
  CHECK(file);
  {
    u8 cfg[224];
    CHECK(fread(cfg, 1, sizeof(cfg), file) == sizeof(cfg));
    CHECK(!manifestopen(cfg, sizeof(cfg), 243, &manifest));
  }
  fclose(file);
  memset(&packet, 0, sizeof(packet));
  packet.header.resource_count = 4;
  packet.entries[0].tag = 1;
  packet.entries[0].handle = H(1);
  packet.entries[1].tag = 2;
  packet.entries[1].handle = H(2);
  packet.entries[2].tag = 3;
  packet.entries[2].handle = H(3);
  packet.entries[3].tag = 7;
  packet.entries[3].handle = H(7);
  memset(&boot, 0, sizeof(boot));
  boot.header = &packet.header;
  boot.resources = packet.entries;
  CHECK(!serviceinit(&manifest, &package, &boot, 1));
  for (i = 0; i < 32 && servicestate[0].stage != 7 && servicestate[0].state != SERVICE_FAILED;
       i++) {
    u32 work = step(now, &next);
    if (!work && next > now)
      now = next;
  }
  if (!strcmp(scenario, "missing") || !strcmp(scenario, "create") || !strcmp(scenario, "map") ||
      !strcmp(scenario, "grant") || !strcmp(scenario, "load") || !strcmp(scenario, "args")) {
    CHECK(!signalmask);
    CHECK(servicephase() == 3 && servicestate[0].reclaimed && !published && !maps);
    CHECK(freed == allocated + (created ? 1 : 0) && destroyed == created);
    return 0;
  }
  CHECK(published && servicephase() == 0 && servicestate[0].stage == 7 && observed);
  if (!strcmp(scenario, "timeout")) {
    now = servicestate[0].deadline;
    step(now, &next);
    CHECK(servicestate[0].error == -ERR_TIMEDOUT);
    CHECK(!terminated && !freed && maps == 3);
    step(now + 1, &next);
    CHECK(terminated == 1 && !freed && maps == 3);
  } else {
    request(INIT_READY, 1, CHILD + 1, 1);
    CHECK((isize) result.words[0] == -ERR_DENIED && !servicestate[0].sequence);
    request(INIT_READY, 1, CHILD, 2);
    CHECK((isize) result.words[0] == -ERR_DENIED);
    if (!strcmp(scenario, "draining")) {
      rqstate = RQ_CHECK_DRAINING;
      request(INIT_READY, 1, CHILD, 1);
      CHECK(finished == 1 && servicestate[0].state == SERVICE_STARTING);
    } else {
      request(INIT_READY, 1, CHILD, 1);
      CHECK(!result.words[0] && result.words[2] == 1 && result.words[3] == 1 &&
            servicephase() == 1);
      request(INIT_READY, 1, CHILD, 1);
      CHECK(!result.words[0]);
      request(INIT_STATUS, 2, CHILD, 1);
      CHECK(!result.words[0] && result.words[2] == SERVICE_READY);
      request(INIT_READY, 1, CHILD, 1);
      CHECK((isize) result.words[0] == -ERR_STALE);
    }
    ended = 1;
    step(10000001, &next);
  }
  CHECK(servicestate[0].state == SERVICE_STOPPING && terminated == 1 && !observed);
  alive = 0;
  serviceexit(CHILD, WAIT_EXITED, WAIT_SIGNALLED | 9);
  step(20000000, &next);
  CHECK(servicephase() == 3 && servicestate[0].reclaimed && !maps && freed == 3 && dropped == 1);
  free(data);
  return 0;
}
