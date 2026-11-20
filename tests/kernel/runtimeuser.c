#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/channel.h>
#include "private/libc.h"
#include "private/syscall.h"
#include "private/virtio/ring.h"
#include "ringuser.h"
#include "fdtableuser.h"
#include "fileuser.h"
#include "stringuser.h"
#include "envuser.h"
volatile u64        runtime_trace[17];
static volatile u64 initialized = UINT64_C(0x123456789abcdef0);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(n);                                                                                    \
  } while (0)
__extension__ typedef long long          Testll;
__extension__ typedef unsigned long long Testull;
typedef struct Numbercase                Numbercase;
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
static int
formatv(char *buffer, size_t size, const char *format, ...)
{
  va_list arguments, copy;
  int     result, needed;
  va_start(arguments, format);
  __builtin_va_copy(copy, arguments);
  result = vsnprintf(buffer, size, format, arguments);
  needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  va_end(arguments);
  return result == needed ? result : -999;
}
static u32
formatcheck(void)
{
  char        buffer[160], guarded[10], raw[3];
  const char *expected;
  int         result, marker = 77;
  u32         checks = 0;
#define FCHECK(c, n)                                                                               \
  do {                                                                                             \
    REQUIRE(c, n);                                                                                 \
    checks++;                                                                                      \
  } while (0)
  errno = EDOM;
  FCHECK(snprintf(buffer, sizeof(buffer), "%+08d:%#x:%#o", -12, 42U, 9U) == 17 &&
             !strcmp(buffer, "-0000012:0x2a:011") && errno == EDOM,
         190);
  FCHECK(snprintf(buffer, sizeof(buffer), "%+08.4d:%-6s", -12, "hi") == 15 &&
             !strcmp(buffer, "   -0012:hi    "),
         191);
  FCHECK(snprintf(buffer, sizeof(buffer), "%.0d:%#.0o:%#.0x", 0, 0U, 0U) == 3 &&
             !strcmp(buffer, ":0:"),
         192);
  FCHECK(snprintf(buffer, sizeof(buffer), "%hhd:%hhu:%hd:%hu", (int) (signed char) -128,
                  (unsigned int) (unsigned char) 255, (int) (short) -32768,
                  (unsigned int) (unsigned short) 65535) == 21 &&
             !strcmp(buffer, "-128:255:-32768:65535"),
         193);
  expected = "-9223372036854775808:18446744073709551615";
  FCHECK(snprintf(buffer, sizeof(buffer), "%ld:%lu", LONG_MIN, ULONG_MAX) ==
                 (int) strlen(expected) &&
             !strcmp(buffer, expected),
         194);
  FCHECK(snprintf(buffer, sizeof(buffer), "%lld:%llu", (Testll) LONG_MIN, (Testull) ULONG_MAX) ==
                 (int) strlen(expected) &&
             !strcmp(buffer, expected),
         195);
  FCHECK(snprintf(buffer, sizeof(buffer), "%jd:%ju", (intmax_t) LONG_MIN, (uintmax_t) ULONG_MAX) ==
                 (int) strlen(expected) &&
             !strcmp(buffer, expected),
         196);
  FCHECK(snprintf(buffer, sizeof(buffer), "%zd:%zu:%td:%tu", (ptrdiff_t) -13, (size_t) 14,
                  (ptrdiff_t) -15, (size_t) 16) == 13 &&
             !strcmp(buffer, "-13:14:-15:16"),
         197);
  expected = "-7:8:-9:10:-9223372036854775808:18446744073709551615:-11:12:-13:14:-15:16:end:!";
  FCHECK(formatv(buffer, sizeof(buffer), "%d:%u:%ld:%lu:%lld:%llu:%jd:%ju:%zd:%zu:%td:%tu:%s:%c",
                 -7, 8U, -9L, 10UL, (Testll) LONG_MIN, (Testull) ULONG_MAX, (intmax_t) -11,
                 (uintmax_t) 12, (ptrdiff_t) -13, (size_t) 14, (ptrdiff_t) -15, (size_t) 16, "end",
                 '!') == (int) strlen(expected) &&
             !strcmp(buffer, expected),
         198);
  FCHECK(formatv(buffer, sizeof(buffer), "[%*.*s]:[%*.*d]", -8, 4, "abcdef", 8, -1, -123) == 21 &&
             !strcmp(buffer, "[abcd    ]:[    -123]"),
         199);
  FCHECK(formatv(buffer, sizeof(buffer), "%.*d", INT_MIN, -123) == 4 && !strcmp(buffer, "-123"),
         200);
  memset(guarded, 0xa5, sizeof(guarded));
  FCHECK(snprintf(guarded + 1, 5, "abcdef") == 6 && !memcmp(guarded + 1, "abcd", 5) &&
             (u8) guarded[0] == 0xa5 && (u8) guarded[6] == 0xa5 && (u8) guarded[9] == 0xa5,
         201);
  FCHECK(snprintf(guarded + 1, 1, "xyz") == 3 && !guarded[1] && (u8) guarded[0] == 0xa5, 202);
  FCHECK(snprintf(NULL, 0, "%s:%+06d", "abc", -42) == 10 && errno == EDOM, 203);
  FCHECK(snprintf(buffer, sizeof(buffer), "A%cB%%", 0) == 4 && !memcmp(buffer, "A\0B%", 5), 204);
  raw[0] = 'a';
  raw[1] = 'b';
  raw[2] = 'c';
  FCHECK(snprintf(buffer, sizeof(buffer), "[%.3s]", raw) == 5 && !strcmp(buffer, "[abc]"), 205);
  FCHECK(formatv(buffer, sizeof(buffer), "[%.0s]", (const char *) 1) == 2 && !strcmp(buffer, "[]"),
         206);
  FCHECK(snprintf(buffer, sizeof(buffer), "%p:%p", (void *) 0, (void *) 0x1234) == 10 &&
             !strcmp(buffer, "0x0:0x1234"),
         207);
  FCHECK(formatv(NULL, 0, "%*s", INT_MAX, "") == INT_MAX && errno == EDOM, 208);
  result = formatv(buffer, 8, "%*s!", INT_MAX, "");
  FCHECK(result == -1 && errno == EOVERFLOW && !buffer[7], 209);
  result = formatv(buffer, sizeof(buffer), "prefix%n", &marker);
  FCHECK(result == -1 && errno == EINVAL && marker == 77 && !strcmp(buffer, "prefix"), 210);
  FCHECK(formatv(buffer, sizeof(buffer), "%ls", (const void *) 1) == -1 && errno == EINVAL &&
             !buffer[0],
         211);
  FCHECK(formatv(buffer, sizeof(buffer), "%f") == -1 && errno == EINVAL && !buffer[0], 212);
  FCHECK(formatv(buffer, sizeof(buffer), "%2147483648.*d") == -1 && errno == EOVERFLOW &&
             !buffer[0],
         213);
#undef FCHECK
  return checks;
}
static u32
groupcheck(void)
{
  pid_t       pid = getpid();
  const pid_t missing = (pid_t) UINT64_C(0x080000000100ffff);
  u32         checks = 0;
#define GCHECK(c, n)                                                                               \
  do {                                                                                             \
    REQUIRE(c, n);                                                                                 \
    checks++;                                                                                      \
  } while (0)
  errno = EDOM;
  GCHECK(pid > (pid_t) UINT32_MAX, 220);
  GCHECK(getpgrp() == 1 && getpgid(0) == 1 && getsid(0) == 1 && errno == EDOM, 221);
  GCHECK(getpgid(pid) == 1 && getsid(pid) == 1 && errno == EDOM, 222);
  GCHECK(getpgid(-1) == -1 && errno == EINVAL, 223);
  GCHECK(getsid(-1) == -1 && errno == EINVAL, 224);
  GCHECK(getpgid(2) == -1 && errno == ESRCH, 225);
  GCHECK(getpgid(missing) == -1 && errno == ESRCH, 226);
  GCHECK(getsid(missing) == -1 && errno == ESRCH, 227);
  GCHECK(setpgid(-1, 0) == -1 && errno == EINVAL, 228);
  GCHECK(setpgid(0, -1) == -1 && errno == EINVAL, 229);
  GCHECK(setpgid(0, 2) == -1 && errno == EPERM && getpgrp() == 1 && errno == EPERM, 230);
  GCHECK(setpgid(missing, 0) == -1 && errno == ESRCH, 231);
  GCHECK(setpgid(0, missing) == -1 && errno == EPERM, 232);
  errno = EDOM;
  GCHECK(!setpgid(0, 0) && getpgrp() == pid && getsid(0) == 1 && errno == EDOM, 233);
  GCHECK(setsid() == -1 && errno == EPERM, 234);
  GCHECK(getpgid(pid) == pid && getsid(pid) == 1 && errno == EPERM, 235);
  errno = EDOM;
  GCHECK(!setpgid(0, 1) && getpgrp() == 1 && errno == EDOM, 236);
  GCHECK(setsid() == pid && getpgrp() == pid && getsid(0) == pid && errno == EDOM, 237);
  GCHECK(setpgid(0, 0) == -1 && errno == EPERM, 238);
  GCHECK(setsid() == -1 && errno == EPERM, 239);
  GCHECK(getpgid(pid) == pid && getsid(pid) == pid && getpgrp() == pid && errno == EPERM, 240);
#undef GCHECK
  return checks;
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
static u32
queuecheck(void)
{
  Vq    q;
  Vused used[VQ_BATCH];
  Vdone done[VQ_BATCH];
  u32   head, i;
  REQUIRE(!vqinit(&q, 7), 220);
  REQUIRE(!vqreserve(&q, VQ_SIZE, 512, 0x1234, &head) && !q.free, 221);
  REQUIRE(!vqpublish(&q, head) && vqcancel(&q, head) == -ERR_STATE, 222);
  used[0].id = head;
  used[0].length = 512;
  REQUIRE(!vqcomplete(&q, 7, used, 1, done) && q.free == VQ_SIZE && done[0].cookie == 0x1234 &&
              done[0].length == 512,
          223);
  for (i = 0; i < VQ_BATCH; i++) {
    REQUIRE(!vqreserve(&q, 1, i, i + 100, &head) && !vqpublish(&q, head), 224);
    used[VQ_BATCH - i - 1].id = head;
    used[VQ_BATCH - i - 1].length = i;
  }
  REQUIRE(!vqcomplete(&q, 7, used, VQ_BATCH, done) && q.free == VQ_SIZE, 225);
  for (i = 0; i < VQ_BATCH; i++)
    REQUIRE(done[i].cookie == VQ_BATCH - i - 1 + 100 && done[i].length == VQ_BATCH - i - 1 &&
                !done[i].reserved,
            226);
  REQUIRE(!vqreserve(&q, 2, 8, 9, &head) && !vqpublish(&q, head), 227);
  used[0].id = used[1].id = head;
  used[0].length = used[1].length = 8;
  done[0].cookie = done[1].cookie = 0xdead;
  REQUIRE(vqcomplete(&q, 7, used, 2, done) == -ERR_PEER && q.broken && q.free == VQ_SIZE - 2 &&
              q.slots[head].state == VQ_PUBLISHED && done[0].cookie == 0xdead &&
              done[1].cookie == 0xdead,
          228);
  return 3;
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
  runtime_trace[9] = formatcheck();
  runtime_trace[10] = groupcheck();
  runtime_trace[11] = queuecheck();
  runtime_trace[12] = ringusercheck();
  runtime_trace[13] = fdtableusercheck();
  runtime_trace[14] = fileusercheck();
  runtime_trace[15] = stringusercheck(&view);
  runtime_trace[16] = envusercheck(&view);
  errno = EDOM;
  runtime_trace[6] = 42;
  if (runtime_trace[4] == 2)
    exit(0x12a);
  if (runtime_trace[4] == 3)
    _exit(0x12a);
  return 42;
}
