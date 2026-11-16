#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/boot.h>
#include "private/elf.h"
#include "private/start.h"
extern const u8       payload_elf[], payload_elf_end[];
volatile u64          elf_trace[16];
typedef struct Packet Packet;
typedef struct Reader Reader;
struct Packet
{
  Boot     header;
  Strref   argv[3], env[2];
  Resentry resources[3];
  char     strings[64];
};
struct Reader
{
  u64 fail_after;
  u32 malformed, reads;
};
static Packet  packet;
static Elfplan plan;
static Handle  pressure[128];
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
stringref(Strref *ref, const char *string, u32 *offset)
{
  u32 bytes = (u32) strlen(string) + 1;
  ref->offset = (u32) offsetof(Packet, strings) + *offset;
  ref->length = bytes;
  memcpy(packet.strings + *offset, string, bytes);
  *offset += bytes;
}
static void
bootpacket(void)
{
  u32 offset = 0;
  memset(&packet, 0, sizeof(packet));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 3;
  packet.header.argv_off = offsetof(Packet, argv);
  packet.header.envc = 2;
  packet.header.env_off = offsetof(Packet, env);
  packet.header.resource_count = 3;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = BOOT_CREATE;
  stringref(&packet.argv[0], "runtime", &offset);
  stringref(&packet.argv[1], "alpha", &offset);
  stringref(&packet.argv[2], "beta", &offset);
  stringref(&packet.env[0], "MODE=1", &offset);
  stringref(&packet.env[1], "X=value", &offset);
}
int
main(int argc, char **argv)
{
  const Bootview *own = sysboot();
  Elfsource       source;
  Elfwork         work;
  Createarg       args;
  Config          config;
  Reader          reader;
  Queryrec        resources[4];
  Maparg          map;
  Allocarg        alloc;
  Handle          create, space, allocator, pid, control, collected, occupied = 0;
  u64             written, next, kind, code, sequence;
  u32             ids[4], scenario, count = 0;
  isize           error;
  int             status;
  REQUIRE(argc == 0 && argv && !argv[0] && getpid() > 1 && getppid() == 1, 181);
  create = bootresource(own, BOOT_CREATE);
  space = bootresource(own, BOOT_SELF_SPACE);
  allocator = bootresource(own, BOOT_SELF_ALLOC);
  REQUIRE(create > 0 && space > 0 && allocator > 0, 182);
  REQUIRE(!__sys_getcred(0, 0, 0, ids), 183);
  source.context = &reader;
  source.bytes = (u64) (payload_elf_end - payload_elf);
  source.read = readpayload;
  alloc.bytes = PAGE_SIZE;
  alloc.kind = ALLOC_DATA;
  alloc.align = alloc.minaddr = alloc.maxaddr = alloc.reserved = 0;
  for (scenario = 0; scenario < 6; scenario++) {
    elf_trace[0] = scenario + 1;
    reader.fail_after = ~(u64) 0;
    reader.malformed = reader.reads = 0;
    REQUIRE(!elfcheck(&source, 64, &plan) && plan.pages > 3 && plan.segments[0].filesz > PAGE_SIZE,
            184);
    memset(&args, 0, sizeof(args));
    args.memlimit = scenario == 2 ? 16 : 64;
    args.exec_limit = 128;
    args.resources = 3;
    args.pending = 1;
    args.ruid = ids[0];
    args.euid = ids[1];
    args.rgid = ids[2];
    args.egid = ids[3];
    memset(&work, 0, sizeof(work));
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, 0, &work.sigmask), 214);
    REQUIRE(!__sys_create(create, &args, &work.view, &pid, &control) && pid > 0 && !control, 185);
    work.pid = pid;
    work.image = 1;
    work.parent_space = space;
    work.scratch = 0x2000000;
    work.scratch_bytes = BOOT_MAX_BYTES;
    elf_trace[8 + scenario] = (u64) pid;
    REQUIRE(!__sys_yield(), 186);
    REQUIRE(!__sys_query(work.view, QUERY_RESOURCE, 0, resources, 4, &written, &next) &&
                written == 3 && !next,
            187);
    elf_trace[5]++;
    bootpacket();
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
      reader.fail_after = plan.segments[0].offset + PAGE_SIZE;
    if (scenario == 4) {
      REQUIRE(!__sys_alloc(allocator, &alloc, &occupied), 191);
      memset(&map, 0, sizeof(map));
      map.mem = occupied;
      map.address = work.scratch;
      map.length = PAGE_SIZE;
      map.prot = MAP_READ | MAP_WRITE;
      REQUIRE(!__sys_map(space, &map), 192);
      *(volatile u64 *) work.scratch = UINT64_C(0x123456789abcdef0);
    }
    if (scenario == 5)
      packet.resources[0].flags = 4;
    error = elfload(&source, &plan, &packet, sizeof(packet), &work, &config);
    REQUIRE(!work.memory && !work.mapped, 193);
    elf_trace[6]++;
    if (scenario <= 1) {
      REQUIRE(!error && !work.failure, 194);
      REQUIRE(!__sys_configure(work.view, &config, 0) && !__sys_start(work.view), 195);
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
      REQUIRE(work.failure == error && !config.entry, 199);
      REQUIRE(!__sys_destroy(work.view), 200);
      REQUIRE(__sys_wait(pid, WAIT_WNOHANG, &collected, &kind, &code, &sequence) == -ERR_CHILD &&
                  !collected,
              201);
      elf_trace[2]++;
    }
    if (scenario == 1)
      while (count)
        REQUIRE(!__sys_free(pressure[--count]), 202);
    if (scenario == 4) {
      REQUIRE(*(volatile u64 *) work.scratch == UINT64_C(0x123456789abcdef0), 203);
      REQUIRE(!__sys_unmap(space, work.scratch) && !__sys_free(occupied), 204);
    }
    REQUIRE(!__sys_yield(), 205);
  }
  reader.malformed = 1;
  reader.fail_after = ~(u64) 0;
  REQUIRE(elfcheck(&source, 64, &plan) == -ERR_INVALID && !plan.entry && !plan.pages, 206);
  elf_trace[3] = 1;
  elf_trace[0] = 99;
  return 42;
}
