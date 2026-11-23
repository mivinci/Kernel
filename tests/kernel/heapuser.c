#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "private/start.h"
#include "private/syscall.h"
#include "private/vm.h"
volatile u64 heap_trace[32];
static void *early;
extern char  __image_end[];
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      heap_trace[31] = (n);                                                                        \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
static void __attribute__((constructor))
constructor(void)
{
  if (heap_trace[1]) {
    early = malloc(512);
    REQUIRE(early && !((usize) early & 15), 201);
    memset(early, 0x55, 512);
  }
}
static void
stringcopies(void)
{
  char  source[] = "argv";
  char  raw[] = {'x', 'y', 'z'};
  char *copy = strdup(source);
  char *bounded = strndup(raw, sizeof(raw));
  char *empty = strndup(source, 0);
  source[0] = '!';
  REQUIRE(copy && !strcmp(copy, "argv") && bounded && !strcmp(bounded, "xyz") && empty && !*empty,
          224);
  free(copy);
  free(bounded);
  free(empty);
}
int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Handle          allocator = bootresource(boot, BOOT_SELF_ALLOC),
         space = bootresource(boot, BOOT_SELF_SPACE);
  Allocarg allocation;
  Maparg   mapping;
  Handle   guard;
  void    *small[64], *a, *b, *c, *large, *zero, *z1, *z2;
  u32      i;
  UNUSED(argc);
  UNUSED(argv);
  heap_trace[13] = (u64) __image_end;
  allocation.bytes = PAGE_SIZE;
  allocation.kind = ALLOC_DATA;
  allocation.align = allocation.minaddr = allocation.maxaddr = allocation.reserved = 0;
  if (!heap_trace[1]) {
    REQUIRE(!early && !__sys_alloc(allocator, &allocation, &guard), 202);
    mapping.mem = guard;
    mapping.offset = 0;
    mapping.address = (u64) __image_end;
    mapping.length = PAGE_SIZE;
    mapping.prot = MAP_READ | MAP_WRITE;
    mapping.reserved = 0;
    REQUIRE(!__sys_map(space, &mapping) && !__sys_free(guard), 203);
    memset(__image_end, 0x7b, PAGE_SIZE);
    heap_trace[10] = mapping.address;
  } else {
    REQUIRE(early && ((u8 *) early)[0] == 0x55 && ((u8 *) early)[511] == 0x55, 204);
    heap_trace[12] = 1;
  }
  errno = EDOM;
  free(NULL);
  stringcopies();
  for (i = 0; i < 64; i++) {
    small[i] = malloc(i + 1);
    REQUIRE(small[i] && !((usize) small[i] & 15) && errno == EDOM, 205);
    memset(small[i], (int) i + 1, i + 1);
    heap_trace[4]++;
  }
  a = malloc(1024);
  b = malloc(1024);
  c = malloc(1024);
  REQUIRE(a && b && c, 206);
  memset(a, 0x5a, 1024);
  free(b);
  REQUIRE(realloc(a, 1800) == a && ((u8 *) a)[1023] == 0x5a, 207);
  REQUIRE(realloc(a, 64) == a && ((u8 *) a)[63] == 0x5a, 208);
  large = realloc(a, 128 * 1024);
  REQUIRE(large && large != a && ((u8 *) large)[0] == 0x5a && ((u8 *) large)[63] == 0x5a, 209);
  free(c);
  memset(large, 0x6d, 128 * 1024);
  a = malloc(2048);
  b = malloc(64);
  REQUIRE(a && b, 210);
  memset(a, 0xdd, 2048);
  free(a);
  zero = calloc(128, 16);
  REQUIRE(zero, 211);
  for (i = 0; i < 2048; i++)
    REQUIRE(!((u8 *) zero)[i], 212);
  free(zero);
  free(b);
  z1 = malloc(0);
  z2 = calloc(0, 123);
  REQUIRE(z1 && z2 && z1 != z2 && realloc(z1, 0) == z1 && errno == EDOM, 213);
  free(z1);
  free(z2);
  REQUIRE(!__sys_alloc(allocator, &allocation, &guard), 214);
  errno = 0;
  REQUIRE(!realloc(large, 4 * 1024 * 1024) && errno == ENOMEM && ((u8 *) large)[0] == 0x6d &&
              ((u8 *) large)[128 * 1024 - 1] == 0x6d,
          215);
  REQUIRE(!__sys_free(guard), 216);
  heap_trace[7] = 1;
  REQUIRE(!malloc((size_t) -1) && errno == ENOMEM && !calloc((size_t) -1, 2) && errno == ENOMEM,
          217);
  heap_trace[8] = (u64) large;
  heap_trace[9] = (u64) small[0];
  heap_trace[11] = 128 * 1024;
  if (heap_trace[10])
    REQUIRE(((u8 *) heap_trace[10])[0] == 0x7b, 218);
  heap_trace[0] = 1;
  REQUIRE(!__sys_yield(), 219);
  errno = EDOM;
  for (i = 0; i < 64; i++) {
    REQUIRE(((u8 *) small[i])[0] == i + 1 && ((u8 *) small[i])[i] == i + 1, 220);
    free(small[i]);
  }
  free(large);
  free(early);
  early = NULL;
  if (heap_trace[10])
    REQUIRE(!__sys_unmap(space, heap_trace[10]), 221);
  REQUIRE(errno == EDOM, 222);
  {
    void *guarded;
    u8    byte = 0xff;
    REQUIRE(!sysvmgetguarded(PAGE_SIZE, &guarded) && !*(u8 *) guarded, 225);
    REQUIRE(__sys_copy(&byte, (void *) ((u64) guarded - PAGE_SIZE), 1) == -ERR_FAULT &&
                byte == 0xff,
            226);
    *(u8 *) guarded = 42;
    REQUIRE(!sysvmput(guarded) && __sys_copy(&byte, guarded, 1) == -ERR_FAULT, 227);
  }
  heap_trace[0] = 2;
  REQUIRE(!__sys_yield(), 223);
  heap_trace[0] = 99;
  return 42;
}
