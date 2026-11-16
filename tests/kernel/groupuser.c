#include <unistd.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 group_trace[32];
extern u32   transportprobe(u64 base);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      group_trace[31] = (n);                                                                       \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
int
main(int argc, char **argv)
{
  Handle memory = bootresource(sysboot(), 257), space = bootresource(sysboot(), BOOT_SELF_SPACE);
  Maparg mapping;
  u32    i;
  Ipcmsg event;
  UNUSED(argc);
  UNUSED(argv);
  mapping.mem = memory;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = 4096;
  mapping.prot = 1;
  mapping.reserved = 0;
  REQUIRE(memory > 0 && group_trace[2] && group_trace[2] <= 8, 151);
  if (group_trace[1] == 1) {
    REQUIRE(__sys_map(space, &mapping) == -ERR_DENIED && !__sys_free(memory), 152);
    REQUIRE(!ipc_notify(bootresource(sysboot(), 258), (u64) 1 << 32), 158);
    group_trace[0] = 99;
    group_trace[3] = 1;
    return 42;
  }
  REQUIRE(!__sys_map(space, &mapping), 153);
  for (i = 0; i < group_trace[2]; i++) {
    REQUIRE(group_trace[4 + i] <= 4096 - 16, 154);
    group_trace[13] = transportprobe(mapping.address + group_trace[4 + i]);
    REQUIRE(group_trace[13] == 2, 154);
    group_trace[12]++;
  }
  REQUIRE(!__sys_free(memory), 155);
  group_trace[0] = 1;
  if (!group_trace[1])
    REQUIRE(!ipc_recv((u64) 1 << 32, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
                event.words[0] == ((u64) 1 << 32),
            156);
  REQUIRE(!__sys_unmap(space, mapping.address), 157);
  group_trace[0] = 99;
  return 42;
}
