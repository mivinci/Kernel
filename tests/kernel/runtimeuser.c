#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/channel.h>
#include "private/libc.h"
#include "private/syscall.h"
volatile u64        runtime_trace[9];
static volatile u64 initialized = UINT64_C(0x123456789abcdef0);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(n);                                                                                    \
  } while (0)
typedef struct Numbercase Numbercase;
struct Numbercase
{
  const char   *text;
  int           base;
  long          signed_value;
  unsigned long unsigned_value;
  u32           end;
  int           signed_error, unsigned_error;
};
static const Numbercase numbers[] = {
    {"", 0, 0, 0, 0, EDOM, EDOM},
    {" \t+", 0, 0, 0, 0, EDOM, EDOM},
    {" -0x2a!", 0, -42, ULONG_MAX - 41, 6, EDOM, EDOM},
    {"0779", 0, 63, 63, 3, EDOM, EDOM},
    {"0xg", 16, 0, 0, 1, EDOM, EDOM},
    {"0b11", 0, 0, 0, 1, EDOM, EDOM},
    {"zz!", 36, 1295, 1295, 2, EDOM, EDOM},
    {"9223372036854775807", 10, LONG_MAX, (unsigned long) LONG_MAX, 19, EDOM, EDOM},
    {"-9223372036854775808", 10, LONG_MIN, (unsigned long) LONG_MAX + 1, 20, EDOM, EDOM},
    {"9223372036854775808", 10, LONG_MAX, (unsigned long) LONG_MAX + 1, 19, ERANGE, EDOM},
    {"-9223372036854775809", 10, LONG_MIN, (unsigned long) LONG_MAX, 20, ERANGE, EDOM},
    {"18446744073709551615", 10, LONG_MAX, ULONG_MAX, 20, ERANGE, EDOM},
    {"-18446744073709551615", 10, LONG_MIN, 1, 21, ERANGE, EDOM},
    {"18446744073709551616", 10, LONG_MAX, ULONG_MAX, 20, ERANGE, ERANGE},
    {"-18446744073709551616", 10, LONG_MIN, ULONG_MAX, 21, ERANGE, ERANGE},
    {"0xffffffffffffffffQ", 0, LONG_MAX, ULONG_MAX, 18, ERANGE, EDOM},
    {"123", 1, 0, 0, 0, EINVAL, EINVAL},
    {"\377"
     "123",
     10, 0, 0, 0, EDOM, EDOM},
    {"12\377"
     "3",
     10, 12, 12, 2, EDOM, EDOM},
    {"-101q", 2, -5, ULONG_MAX - 4, 4, EDOM, EDOM}};
STATIC_ASSERT(sizeof(long) == 8, runtime_integer_lp64);
static u32
numbercheck(void)
{
  char       *end;
  u32         i;
  int         base;
  const char *text = "10!";
  for (i = 0; i < ARRAY_SIZE(numbers); i++) {
    const Numbercase *test = &numbers[i];
    errno = EDOM;
    REQUIRE(strtol(test->text, &end, test->base) == test->signed_value &&
                end == test->text + test->end && errno == test->signed_error,
            180);
    errno = EDOM;
    REQUIRE(strtoul(test->text, &end, test->base) == test->unsigned_value &&
                end == test->text + test->end && errno == test->unsigned_error,
            181);
  }
  for (base = 2; base <= 36; base++) {
    errno = EDOM;
    REQUIRE(strtol(text, &end, base) == base && end == text + 2 && errno == EDOM, 182);
    REQUIRE(strtoul(text, &end, base) == (unsigned long) base && end == text + 2 && errno == EDOM,
            183);
  }
  return (u32) ARRAY_SIZE(numbers) + 35;
}
static void
preinit(void)
{
  REQUIRE(environ && !runtime_trace[0] && !runtime_trace[1], 150);
  runtime_trace[1] = 1;
}
static Exitfn before ALIGNED(8) __attribute__((section(".preinit_array"), used)) = preinit;
static void __attribute__((constructor(150)))
first(void)
{
  REQUIRE(runtime_trace[1] == 1 && initialized == UINT64_C(0x123456789abcdef0), 151);
  runtime_trace[1] = 12;
}
static void __attribute__((constructor))
second(void)
{
  REQUIRE(runtime_trace[1] == 12 && environ, 152);
  runtime_trace[1] = 123;
}
static void
counted(void)
{
  runtime_trace[7]++;
}
static void
callbackone(void)
{
  REQUIRE(runtime_trace[2] == 2, 153);
  runtime_trace[2] = 21;
}
static void
callbacktwo(void)
{
  REQUIRE(!runtime_trace[2] && errno == EDOM, 154);
  runtime_trace[2] = 2;
  REQUIRE(atexit(counted) != 0 && errno == EBUSY, 155);
}
static void __attribute__((destructor))
finishlast(void)
{
  REQUIRE(runtime_trace[2] == 21 && !runtime_trace[3], 156);
  REQUIRE(runtime_trace[7] == (runtime_trace[4] == 1 ? 30 : 0), 157);
  runtime_trace[3] = 1;
}
static void __attribute__((destructor(150)))
finishfirst(void)
{
  REQUIRE(runtime_trace[3] == 1, 158);
  runtime_trace[3] = 12;
}
int
main(int argc, char **argv)
{
  Bootview    view;
  const char *mode;
  char        scratch[128], other[128];
  int         i;
  REQUIRE(runtime_trace[1] == 123 && !runtime_trace[0], 159);
  runtime_trace[0] = 1;
  mode = getenv("MODE");
  runtime_trace[4] = mode ? (u64) (mode[0] - '0') : argc ? 6 : 0;
  REQUIRE(!bootcheck(libcboot(), libcboot()->bytes, &view), 160);
  REQUIRE(argv && !argv[argc] && environ, 161);
  REQUIRE(!getenv("") && !getenv("X=") && !getenv("MISSING"), 162);
  if (runtime_trace[4] == 1) {
    REQUIRE(argc == 3 && !strcmp(argv[0], "runtime") && !strcmp(argv[1], "alpha") &&
                !strcmp(argv[2], "beta") && !strcmp(getenv("X"), "value"),
            163);
    argv[1][0] = 'A';
    getenv("X")[0] = 'V';
    REQUIRE(!strcmp(argv[1], "Alpha") && !strcmp(getenv("X"), "Value"), 164);
    REQUIRE(bootarg(&view, 1, 0)[0] == 'a' && bootarg(&view, 1, 1)[2] == 'v', 165);
    for (i = 0; i < 30; i++)
      REQUIRE(!atexit(counted), 166);
  } else if (runtime_trace[4] == 6) {
    REQUIRE(argc == 1 && strlen(argv[0]) == BOOT_ARG_BYTES - 25 && !environ[0], 167);
    argv[0][0] = 'Z';
    argv[0][BOOT_ARG_BYTES - 26] = 'Z';
    REQUIRE(bootarg(&view, 0, 0)[0] == 'A' && bootarg(&view, 0, 0)[BOOT_ARG_BYTES - 26] == 'A',
            168);
  } else if (!runtime_trace[4])
    REQUIRE(argc == 0 && !argv[0] && !environ[0], 169);
  else
    REQUIRE(argc == 1 && !strcmp(argv[0], "runtime"), 170);
  if (bootresource(&view, BOOT_CREATE)) {
    Queryrec entries[4];
    Allocarg request;
    Handle   memory;
    u64      count, next;
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, 0, entries, 4, &count, &next) && count == 3 && !next,
            177);
    REQUIRE((Handle) entries[0].data[0] == bootresource(&view, BOOT_SELF_SPACE) &&
                (Handle) entries[1].data[0] == bootresource(&view, BOOT_SELF_ALLOC) &&
                (Handle) entries[2].data[0] == bootresource(&view, BOOT_CREATE),
            178);
    request.bytes = PAGE_SIZE;
    request.kind = ALLOC_DATA;
    request.align = request.minaddr = request.maxaddr = request.reserved = 0;
    REQUIRE(__sys_alloc(bootresource(&view, BOOT_SELF_ALLOC), &request, &memory) == -ERR_CAPACITY &&
                !memory,
            179);
  }
  runtime_trace[5] = 1;
  REQUIRE(!atexit(callbackone) && !atexit(callbacktwo), 171);
  if (runtime_trace[4] == 1)
    REQUIRE(atexit(counted) != 0 && errno == ENOMEM, 172);
  errno = EDOM;
  REQUIRE(!strcmp(strerror(ENOMEM), "Not enough memory") && errno == EDOM &&
              !strcmp(strerror(9999), "Unknown error"),
          173);
  memset(scratch, 0xa5, sizeof(scratch));
  REQUIRE(memcpy(other, scratch, sizeof(scratch)) == other &&
              !memcmp(other, scratch, sizeof(scratch)),
          174);
  memcpy(scratch, "abcdef", 7);
  memmove(scratch + 1, scratch, 6);
  REQUIRE(!memcmp(scratch, "aabcdef", 7), 175);
  memmove(scratch, scratch + 1, 6);
  REQUIRE(!memcmp(scratch, "abcdef", 6) && strchr("abc", 'b')[0] == 'b' &&
              strchr("abc", 0)[0] == 0 && !strchr("abc", 'z') && strcmp("\377", "a") > 0,
          176);
  runtime_trace[8] = numbercheck();
  errno = EDOM;
  runtime_trace[6] = 42;
  if (runtime_trace[4] == 2)
    exit(0x12a);
  if (runtime_trace[4] == 3)
    _exit(0x12a);
  return 42;
}
