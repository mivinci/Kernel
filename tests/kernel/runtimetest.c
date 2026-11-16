#include "ktest.h"
#include "load.h"
#include <diag.h>
#include <abi/boot.h>
extern const u8  runtime_image[], runtime_image_end[];
extern const u64 runtime_trace_address;
static u8        packet[BOOT_MAX_BYTES] ALIGNED(8);
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("RUNTIMEFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("C startup runtime");                                                                  \
    }                                                                                              \
  } while (0)
static void
append(u64 *cursor, Strref *ref, const char *string)
{
  u64 size = 0;
  do {
    size++;
  } while (string[size - 1]);
  ref->offset = (u32) *cursor;
  ref->length = (u32) size;
  while (size--)
    packet[(*cursor)++] = (u8) *string++;
}
static u64
startup(Handle pid, u32 mode)
{
  Rtable   *table = proctable(pid);
  Boot     *boot = (Boot *) packet;
  Strref   *argv, *env;
  Resentry *resources;
  Bootfd   *fd;
  Procinfo  info;
  Handle    call;
  u64       cursor, i;
  char      role[] = "MODE=0";
  for (i = 0; i < sizeof(packet); i++)
    packet[i] = 0;
  boot->magic = BOOT_MAGIC;
  boot->format = BOOT_FORMAT;
  boot->user_lo = BOOT_USER_LO;
  boot->user_hi = BOOT_USER_HI;
  cursor = sizeof(Boot);
  boot->argc = !mode ? 0 : mode == 1 ? 3 : 1;
  boot->envc = !mode || mode == 6 || mode == 7 ? 0 : mode == 1 ? 2 : 1;
  if (boot->argc) {
    boot->argv_off = cursor;
    cursor += boot->argc * sizeof(Strref);
  }
  if (boot->envc) {
    boot->env_off = cursor;
    cursor += boot->envc * sizeof(Strref);
  }
  argv = (Strref *) (packet + boot->argv_off);
  env = (Strref *) (packet + boot->env_off);
  boot->resource_count = 2;
  boot->resources_off = cursor;
  resources = (Resentry *) (packet + cursor);
  cursor += 2 * sizeof(Resentry);
  resources[0].tag = BOOT_SELF_SPACE;
  resources[0].handle = testresource(table, LOCAL_SPACE);
  resources[1].tag = BOOT_SELF_ALLOC;
  resources[1].handle = testresource(table, LOCAL_ALLOC);
  if (mode == 5) {
    REQUIRE(!procinfo(pid, &info));
    REQUIRE(!localendpoint(table, info.identity, LOCAL_CALL, LOCAL_USE, 0, 0, &call));
    boot->fd_count = 1;
    boot->fds_off = cursor;
    fd = (Bootfd *) (packet + cursor);
    fd->backend_call = call;
    fd->session = fd->prepare_token = 1;
    cursor += sizeof(Bootfd);
  }
  if (mode == 9) {
    boot->fix_off = cursor;
    boot->fix_bytes = 64;
    *(u64 *) (packet + cursor) = 1;
    cursor += 64;
  }
  if (mode == 6 || mode == 7) {
    argv[0].offset = (u32) cursor;
    argv[0].length = BOOT_ARG_BYTES - 24 + (mode == 7);
    for (i = 0; i < argv[0].length - 1; i++)
      packet[cursor++] = 'A';
    packet[cursor++] = 0;
  } else if (mode) {
    append(&cursor, &argv[0], "runtime");
    if (mode == 1) {
      append(&cursor, &argv[1], "alpha");
      append(&cursor, &argv[2], "beta");
    }
  }
  if (boot->envc) {
    role[5] = (char) ('0' + mode);
    append(&cursor, &env[0], role);
    if (mode == 1)
      append(&cursor, &env[1], "X=value");
  }
  if (mode == 4)
    packet[argv[0].offset + argv[0].length - 1] = 1;
  if (mode == 8)
    resources[0].flags = 4;
  boot->bytes = cursor;
  return cursor;
}
void
runtimetest(void)
{
  Setid      set;
  Set        account;
  Handle     pid, realm, ended_group, pgid, sid;
  Group      relationship;
  Procinfo   info;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[11], bytes;
  u32        mode, i, good, mains = 0, rejected = 0, callbacks = 0, finalizers = 0, numbers = 0,
                     formats = 0, groups = 0;
  int error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (mode = 0; mode < 10; mode++) {
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid) && !procinfo(pid, &info));
    REQUIRE(!lifestat(info.life, &life) && !grouphold(life.group));
    realm = life.group;
    bytes = startup(pid, mode);
    testload(pid, runtime_image, (u64) (runtime_image_end - runtime_image), packet, bytes);
    REQUIRE(!procpublish(pid) && !procrunone());
    REQUIRE(!lifestat(info.life, &life));
    good = mode <= 3 || mode == 6;
    if (life.state != LIFE_ENDED || life.reason != LIFE_EXIT || life.code != (good ? 42 : 127)) {
      putstr("RUNTIMERESULT mode=");
      puthex(mode);
      putstr(" reason=");
      puthex(life.reason);
      putstr(" code=");
      puthex(life.code);
      putstr("\n");
      panic("runtime exit outcome");
    }
    REQUIRE(!spaceread(info.space, runtime_trace_address, trace, sizeof(trace)));
    if (!good) {
      for (i = 0; i < ARRAY_SIZE(trace); i++)
        REQUIRE(!trace[i]);
      rejected++;
    } else {
      REQUIRE(trace[0] == 1 && trace[1] == 123 && trace[4] == mode && trace[5] == 1 &&
              trace[6] == 42 && trace[8] == 55 && trace[9] == 24 && trace[10] == 21);
      REQUIRE(trace[2] == (mode == 3 ? 0 : 21) && trace[3] == (mode == 3 ? 0 : 12));
      REQUIRE(trace[7] == (mode == 1 ? 30 : 0));
      mains++;
      numbers += (u32) trace[8];
      formats += (u32) trace[9];
      groups += (u32) trace[10];
      REQUIRE(!groupnames(life.group, &pgid, &sid) && pgid == pid && sid == pid);
      if (mode != 3) {
        callbacks += 2 + (u32) trace[7];
        finalizers += 2;
      }
    }
    ended_group = life.group;
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(info.life, 1, &result));
    REQUIRE(!groupdrop(realm) && groupstat(realm, &relationship) == -ERR_BAD_HANDLE &&
            groupstat(ended_group, &relationship) == -ERR_BAD_HANDLE);
  }
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !groupcheck());
  putstr("RUNTIMECHECK cases=");
  puthex(10);
  putstr(" main=");
  puthex(mains);
  putstr(" rejected=");
  puthex(rejected);
  putstr(" callbacks=");
  puthex(callbacks);
  putstr(" finalizers=");
  puthex(finalizers);
  putstr(" numbers=");
  puthex(numbers);
  putstr(" formats=");
  puthex(formats);
  putstr(" groups=");
  puthex(groups);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
