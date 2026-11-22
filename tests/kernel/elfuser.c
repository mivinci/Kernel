#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/boot.h>
#include "private/elf.h"
#include "private/start.h"
#include "private/argument.h"
extern const u8       payload_elf[], payload_elf_end[];
volatile u64          elf_trace[16];
typedef struct Packet Packet;
typedef struct Reader Reader;
struct Packet
{
  Boot     header;
  Resentry resources[3];
  Bootfd   fd;
  u8       arguments[BOOT_ARG_BYTES];
};
struct Reader
{
  u64 fail_after;
  u32 malformed, reads;
};
static Packet  packet;
static Elfplan plan;
static Handle  pressure[128];
static u64     argument_storage[BOOT_ARG_BYTES / sizeof(u64)];
static char    argument_word[] = "alpha";
static char    environment_word[] = "MODE=1";
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      exit(n);                                                                                     \
  } while (0)
static isize
readpayload(void *context, u64 offset, void *destination, u64 bytes)
{
  Reader *reader = context;
  reader->reads++;
  if (offset >= reader->fail_after)
    return -ERR_FAULT;
  memcpy(destination, payload_elf + offset, bytes);
  if (reader->malformed && !offset)
    ((u8 *) destination)[0] = 0;
  return 0;
}
static void
bootpacket(Handle fdcall, char mode)
{
  const char *argv[4], *envp[3];
  Argsnap     snapshot;
  u64         mask, ignored;
  int         replacing = mode == '4';
  argument_word[0] = 'a';
  environment_word[0] = 'M';
  environment_word[5] = mode;
  argv[0] = "runtime";
  argv[1] = argument_word;
  argv[2] = "beta";
  argv[3] = NULL;
  envp[0] = environment_word;
  envp[1] = "X=value";
  envp[2] = NULL;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysargcopy(argument_storage, sizeof(argument_storage), argv, envp, &snapshot),
          215);
  argument_word[0] = 'X';
  environment_word[0] = 'Y';
  memset(&packet, 0, sizeof(packet));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = offsetof(Packet, arguments);
  packet.header.resource_count = replacing ? 2 : 3;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = BOOT_CREATE;
  if (fdcall) {
    packet.header.fd_count = 1;
    packet.header.fds_off = offsetof(Packet, fd);
    packet.fd.backend_call = fdcall;
    packet.fd.session = packet.fd.prepare_token = 1;
  }
  REQUIRE(!sysargboot(&snapshot, &packet.header, sizeof(packet)) && packet.header.argc == 3 &&
              packet.header.envc == 2 && packet.header.bytes < PAGE_SIZE &&
              argument_word[0] == 'X' && environment_word[0] == 'Y' &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          216);
  /* Preparing SELF entries remain zero until the loader resolves them. */
  elf_trace[14]++;
}
int
main(int argc, char **argv)
{
  const Bootview *own = sysboot();
  Elfsource       source;
  Spawnwork       spawn;
  Elfwork        *work = &spawn.elf;
  Execwork        replacement = {0};
  Createarg       args;
  Spawnsig        signals = {UINT64_C(1) << (SIGNAL_USR2 - 1), UINT64_C(1) << (SIGNAL_USR1 - 1), 1};
  Sigact          ignore = {0};
  Reader          reader;
  Queryrec        resources[4];
  Maparg          map;
  Allocarg        alloc;
  Handle          create, space, allocator, pid, control, collected, occupied = 0;
  u64             written, next, kind, code, sequence, mask;
  u32             ids[4], scenario, count = 0, retries = 0;
  isize           error;
  int             status;
  REQUIRE(argc == 0 && argv && !argv[0] && getpid() > 1 && getppid() == 1, 181);
  create = bootresource(own, BOOT_CREATE);
  space = bootresource(own, BOOT_SELF_SPACE);
  allocator = bootresource(own, BOOT_SELF_ALLOC);
  REQUIRE(create > 0 && space > 0 && allocator > 0, 182);
  REQUIRE(!__sys_getcred(0, 0, 0, ids), 183);
  errno = EDOM;
  REQUIRE(getuid() == ids[0] && geteuid() == ids[1] && getgid() == ids[2] && getegid() == ids[3] &&
              errno == EDOM,
          183);
  source.context = &reader;
  source.bytes = (u64) (payload_elf_end - payload_elf);
  source.read = readpayload;
  ignore.kind = SIGNAL_IGNORE;
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, &ignore, NULL) &&
              !__sys_sigmask(SIGNAL_SETMASK, signals.defaults, &mask),
          214);
  alloc.bytes = PAGE_SIZE;
  alloc.kind = ALLOC_DATA;
  alloc.align = alloc.minaddr = alloc.maxaddr = alloc.reserved = 0;
  for (scenario = 0; scenario < 6; scenario++) {
    elf_trace[0] = scenario + 1;
    reader.fail_after = ~(u64) 0;
    reader.malformed = reader.reads = 0;
    REQUIRE(!elfcheck(&source, 64, &plan) && plan.pages > 3 && plan.count > 1 &&
                plan.segments[1].filesz,
            184);
    memset(&args, 0, sizeof(args));
    /* Success fixtures fund the selected ELF plus stack/Boot/kernel metadata;
       the explicit underfunded case remains independent of payload growth. */
    args.memlimit = scenario == 2 ? 16 : plan.pages + 64;
    args.exec_limit = 128;
    args.resources = 3;
    args.pending = 1;
    args.ruid = ids[0];
    args.euid = ids[1];
    args.rgid = ids[2];
    args.egid = ids[3];
    memset(&spawn, 0, sizeof(spawn));
    work->parent_space = space;
    work->scratch = 0x2000000;
    work->scratch_bytes = BOOT_MAX_BYTES;
    REQUIRE(!sysspawnprepare(&spawn, create, &args, scenario == 0 ? &signals : NULL, &control) &&
                work->pid > 0 && !control && sysspawnpublish(&spawn) == -ERR_STATE,
            185);
    pid = work->pid;
    elf_trace[8 + scenario] = (u64) pid;
    REQUIRE(!__sys_yield(), 186);
    REQUIRE(!__sys_query(work->view, QUERY_RESOURCE, 0, resources, 4, &written, &next) &&
                written == 3 && !next,
            187);
    elf_trace[5]++;
    bootpacket(scenario == 5 ? (Handle) resources[0].data[0] : 0, scenario == 1 ? '2' : '1');
    if (scenario == 1) {
      count = 0;
      while (!(error = __sys_alloc(allocator, &alloc, &pressure[count]))) {
        count++;
        REQUIRE(count < ARRAY_SIZE(pressure), 188);
      }
      REQUIRE(error == -ERR_CAPACITY && count > 1, 189);
      elf_trace[4] = count;
      REQUIRE(!__sys_free(pressure[--count]), 190);
    }
    if (scenario == 3)
      reader.fail_after = plan.segments[1].offset;
    if (scenario == 4) {
      REQUIRE(!__sys_alloc(allocator, &alloc, &occupied), 191);
      memset(&map, 0, sizeof(map));
      map.mem = occupied;
      map.address = work->scratch;
      map.length = PAGE_SIZE;
      map.prot = MAP_READ | MAP_WRITE;
      REQUIRE(!__sys_map(space, &map), 192);
      *(volatile u64 *) work->scratch = UINT64_C(0x123456789abcdef0);
    }
    if (scenario == 5) {
      Config rejected;
      REQUIRE(elfload(&source, &plan, &packet, packet.header.bytes, work, &rejected) ==
                      -ERR_BAD_HANDLE &&
                  work->failure == -ERR_BAD_HANDLE && !work->memory && !work->mapped &&
                  !rejected.entry,
              217);
      packet.header.fd_count = packet.header.fds_off = 0;
      packet.resources[0].flags = 4;
      error = elfload(&source, &plan, &packet, packet.header.bytes, work, &rejected);
    } else {
      error = sysspawnfill(&spawn, &source, &plan, &packet, packet.header.bytes);
    }
    elf_trace[15] = (u64) error;
    REQUIRE(!work->memory && !work->mapped, 193);
    elf_trace[6]++;
    if (scenario <= 1) {
      REQUIRE(!error && !work->failure && !__sys_sigmask(SIGNAL_BLOCK, 0, &mask) &&
                  mask == signals.defaults &&
                  spawn.config.sigmask == (scenario == 0 ? signals.mask : signals.defaults),
              194);
      source.read = NULL;
      memset(&packet, 0, sizeof(packet));
      REQUIRE(sysspawnpublish(&spawn) == pid && !work->view, 195);
      source.read = readpayload;
      status = 0x12345678;
      errno = EDOM;
      REQUIRE(waitpid(pid, &status, WNOHANG) == 0 && status == 0x12345678 && errno == EDOM, 196);
      if (scenario == 0) {
        REQUIRE(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 42 &&
                    !WIFSIGNALED(status) && !WIFSTOPPED(status) && !WIFCONTINUED(status) &&
                    errno == EDOM,
                197);
      } else {
        REQUIRE(waitpid(pid, (int *) own->header, 0) == -1 && errno == EFAULT, 207);
        REQUIRE(own->header->magic == BOOT_MAGIC && status == 0x12345678, 208);
      }
      REQUIRE(waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD, 209);
      REQUIRE(wait(NULL) == -1 && errno == ECHILD, 210);
      elf_trace[1]++;
      elf_trace[7]++;
    } else {
      REQUIRE(error == (scenario == 2   ? -ERR_LIMIT
                        : scenario == 3 ? -ERR_FAULT
                        : scenario == 4 ? -ERR_STATE
                                        : -ERR_INVALID),
              198);
      REQUIRE(work->failure == error && !spawn.config.entry &&
                  sysspawnpublish(&spawn) == -ERR_STATE,
              199);
      REQUIRE(!sysspawncancel(&spawn) && !work->view, 200);
      REQUIRE(__sys_wait(pid, WAIT_WNOHANG, &collected, &kind, &code, &sequence) == -ERR_CHILD &&
                  !collected,
              201);
      elf_trace[2]++;
    }
    if (scenario == 1)
      while (count)
        REQUIRE(!__sys_free(pressure[--count]), 202);
    if (scenario == 4) {
      REQUIRE(*(volatile u64 *) work->scratch == UINT64_C(0x123456789abcdef0), 203);
      REQUIRE(!__sys_unmap(space, work->scratch) && !__sys_free(occupied), 204);
    }
    REQUIRE(!__sys_yield(), 205);
  }
  reader.malformed = 1;
  reader.fail_after = ~(u64) 0;
  REQUIRE(elfcheck(&source, 64, &plan) == -ERR_INVALID && !plan.entry && !plan.pages, 206);
  elf_trace[3] = 1;
  reader.malformed = 0;
  REQUIRE(!elfcheck(&source, 64, &plan), 230);
  bootpacket(0, '4');
  replacement.elf.parent_space = space;
  replacement.elf.scratch = 0x2000000;
  replacement.elf.scratch_bytes = BOOT_MAX_BYTES;
  reader.fail_after = plan.segments[1].offset;
  REQUIRE(!sysexecprepare(&replacement, 0) && sysexecpublish(&replacement) == -ERR_STATE &&
              sysexecfill(&replacement, &source, &plan, &packet, packet.header.bytes) ==
                  -ERR_FAULT &&
              replacement.elf.view && !replacement.elf.memory && !replacement.elf.mapped &&
              replacement.elf.failure == -ERR_FAULT && !sysexeccancel(&replacement) &&
              !replacement.elf.view,
          231);
  reader.fail_after = ~(u64) 0;
  elf_trace[0] = 99;
  REQUIRE(!__sys_yield(), 232);
  do {
    error = sysexecprepare(&replacement, 0);
    if (error == -ERR_BUSY)
      REQUIRE(++retries < 64 && !__sys_yield(), 232);
  } while (error == -ERR_BUSY);
  REQUIRE(!error && !sysexecfill(&replacement, &source, &plan, &packet, packet.header.bytes), 234);
  /* After sealing the target the loader no longer needs the source or Boot. */
  source.read = NULL;
  memset(&packet, 0, sizeof(packet));
  REQUIRE(!__sys_yield(), 235);
  error = sysexecpublish(&replacement);
  REQUIRE(!sysexeccancel(&replacement), 236);
  elf_trace[15] = (u64) error;
  return 233;
}
