#include <unistd.h>
#include "private/syscall.h"
#include "private/start.h"
volatile u64 cow_trace[32];
extern u64   cow_store(volatile u64 *address, u64 value);
extern u64   frame_fp(u64 mode);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      cow_trace[31] = (n);                                                                         \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
int
main(int argc, char **argv)
{
  volatile u64 *a = (volatile u64 *) 0x2000000, *b = (volatile u64 *) 0x2100000,
               *readonly = (volatile u64 *) 0x2200000;
  u64 role = cow_trace[1], first = 0x123400 + role, second = 0x567800 + role;
  UNUSED(argc);
  UNUSED(argv);
  {
    Handle memory = bootresource(sysboot(), 257), space = bootresource(sysboot(), BOOT_SELF_SPACE);
    Maparg mapping;
    if (memory) {
      mapping.mem = memory;
      mapping.offset = 0;
      mapping.length = 8192;
      mapping.reserved = 0;
      mapping.prot = MAP_READ | MAP_WRITE;
      mapping.address = 0x2000000;
      REQUIRE(!__sys_map(space, &mapping), 162);
      mapping.address = 0x2100000;
      REQUIRE(!__sys_map(space, &mapping), 163);
      mapping.prot = MAP_READ;
      mapping.address = 0x2200000;
      REQUIRE(!__sys_map(space, &mapping) && !__sys_free(memory), 164);
      cow_trace[7] = 1;
    }
  }
  cow_trace[0] = 1;
  REQUIRE(a[0] == 0x41 && b[0] == 0x41 && readonly[0] == 0x41 && a[512] == 0x42 &&
              readonly[512] == 0x42 && !a[1] && !a[511],
          151);
  if (role == 2) {
    cow_trace[0] = 99;
    return 42;
  }
  if (role == 3) {
    cow_store(readonly, 0xdead);
    REQUIRE(0, 152);
  }
  REQUIRE(frame_fp(1) == 0, 153);
  REQUIRE(cow_store(a, first) == 1 && frame_fp(1) == 1, 154);
  REQUIRE(a[0] == first && b[0] == first && readonly[0] == first, 155);
  b[1] = first + 1;
  REQUIRE(a[1] == first + 1 && readonly[1] == first + 1, 156);
  REQUIRE(cow_store(b + 512, second) == 1 && frame_fp(1) == 1, 157);
  REQUIRE(a[512] == second && readonly[512] == second, 158);
  a[513] = second + 1;
  REQUIRE(b[513] == second + 1 && readonly[513] == second + 1 && !a[511] && !a[1023], 159);
  cow_trace[4] = 2;
  cow_trace[5] = first;
  cow_trace[6] = second;
  cow_trace[0] = 2;
  REQUIRE(!__sys_yield(), 160);
  REQUIRE(a[0] == first && b[512] == second && readonly[1] == first + 1 && frame_fp(1) == 1, 161);
  cow_trace[0] = 99;
  return 42;
}
