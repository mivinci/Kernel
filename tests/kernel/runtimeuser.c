#include <ctype.h>
#include <endian.h>
#include <fnmatch.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/channel.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include "private/libc.h"
#include "private/pathsearch.h"
#include "private/spawnfiles.h"
#include "private/syscall.h"
#include "private/timer.h"
#include "private/virtio/ring.h"
#include "ringuser.h"
#include "fdtableuser.h"
#include "fileuser.h"
#include "stringuser.h"
#include "envuser.h"
#include "../../sbin/fs/path.h"
#include "../../sbin/fs/pipe.h"

int vwalkusercheck(void);

#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(n);                                                                                    \
  } while (0)

static int
spawnfile(void *context, const Spawnaction *action)
{
  u32 *step = context;
  switch ((*step)++) {
  case 0:
    REQUIRE(action->kind == SPAWNFILE_OPEN && action->fd == 4 && !strcmp(action->path, "log") &&
                action->flags == (O_CREAT | O_WRONLY) && action->mode == 0600,
            296);
    return 0;
  case 1:
    REQUIRE(action->kind == SPAWNFILE_DUP2 && action->fd == 4 && action->target == 1, 297);
    return EACCES;
  default:
    return EIO;
  }
}

static int
spawncandidate(void *context, const char *path)
{
  u32 *step = context;
  if (!(*step)++) {
    REQUIRE(!strcmp(path, "/bin/echo"), 298);
    return EACCES;
  }
  REQUIRE(!strcmp(path, "echo"), 299);
  return 0;
}

typedef struct Spawnfixture Spawnfixture;
struct Spawnfixture
{
  Fdtable  table;
  Filepool pool;
  u64      opened;
};

static int
spawnopen(void *context, const char *path, int flags, mode_t mode, u64 *file)
{
  Spawnfixture *fixture = context;
  Conntag       connection = {1, 0, 0};
  REQUIRE(!strcmp(path, "log") && flags == O_CLOEXEC && mode == 0 &&
              !sysfilerefnew(&fixture->pool, fixture->pool.self, connection, 6, 7, 0, file),
          303);
  fixture->opened = *file;
  return 0;
}

static void
spawnchildcheck(void)
{
  Spawnfixture              *fixture = malloc(sizeof(*fixture));
  posix_spawn_file_actions_t actions;
  Connpeer                   self = {1, 1};
  Conntag                    connection = {1, 0, 0};
  Fdview                     fd, old;
  Fileref                    ref;
  u64                        inherited;
  REQUIRE(fixture, 304);
  sysfdinit(&fixture->table);
  REQUIRE(
      !sysfilerefinit(&fixture->pool, self) &&
          !sysfilerefnew(&fixture->pool, self, connection, 4, 5, 0, &inherited) &&
          !sysfdobserve(&fixture->table, 4, &fd) &&
          !sysfdreplace(&fixture->table, fd.tag, inherited, FD_CLOEXEC, &old) &&
          !posix_spawn_file_actions_init(&actions) &&
          !posix_spawn_file_actions_adddup2(&actions, 4, 1) &&
          !posix_spawn_file_actions_adddup2(&actions, 4, 4) &&
          !posix_spawn_file_actions_addopen(&actions, 1, "log", O_CLOEXEC, 0) &&
          !posix_spawn_file_actions_addclose(&actions, 7) &&
          !spawnfilesprepare(&actions, &fixture->table, &fixture->pool, self, spawnopen, fixture),
      305);
  REQUIRE(!sysfdobserve(&fixture->table, 1, &fd) && fd.tag.state == SYSFD_FREE &&
              !sysfdobserve(&fixture->table, 4, &fd) && fd.file == inherited && !fd.flags &&
              !sysfilerefview(&fixture->pool, self, inherited, &ref) && ref.aliases == 1 &&
              !sysfilerefview(&fixture->pool, self, fixture->opened, &ref) &&
              ref.state == SYSFILE_REF_CLOSING && !posix_spawn_file_actions_destroy(&actions),
          306);
  free(fixture);
}

static void
spawncheck(void)
{
  posix_spawn_file_actions_t actions;
  static char                candidate[PATHSEARCH_BYTES];
  char                       path[] = "log";
  u32                        step = 0;
  errno = EDOM;
  REQUIRE(!posix_spawn_file_actions_init(&actions) &&
              posix_spawn_file_actions_addclose(&actions, -1) == EBADF &&
              !posix_spawn_file_actions_addopen(&actions, 4, path, O_CREAT | O_WRONLY, 0600) &&
              !posix_spawn_file_actions_adddup2(&actions, 4, 1) &&
              !posix_spawn_file_actions_addclose(&actions, 4),
          300);
  path[0] = 'x';
  REQUIRE(spawnfilesapply(&actions, spawnfile, &step) == EACCES && step == 2 &&
              !posix_spawn_file_actions_destroy(&actions) && errno == EDOM,
          301);
  step = 0;
  REQUIRE(!pathsearch("echo", "/bin:", candidate, spawncandidate, &step) && step == 2, 302);
}

static void
pathwalkcheck(void)
{
  char     path[] = "//a/link/../file";
  char     longpath[FS_PATH_BYTES];
  Fspath   walk;
  Pathpart part;
  u32      i;
  REQUIRE(!fspathinit(&walk, path, sizeof(path)), 285);
  path[2] = 'x';
  REQUIRE(fspathnext(&walk, &part) == 1 && part.kind == PATH_ROOT && part.directory &&
              !part.final && fspathnext(&walk, &part) == 1 && part.kind == PATH_NAME &&
              part.bytes == 1 && part.name[0] == 'a' && part.directory &&
              fspathnext(&walk, &part) == 1 && part.bytes == 4 && !memcmp(part.name, "link", 4) &&
              !fspathexpand(&walk, "/b/c", sizeof("/b/c")),
          286);
  REQUIRE(fspathnext(&walk, &part) == 1 && part.kind == PATH_ROOT &&
              fspathnext(&walk, &part) == 1 && part.bytes == 1 && part.name[0] == 'b' &&
              fspathnext(&walk, &part) == 1 && part.bytes == 1 && part.name[0] == 'c' &&
              fspathnext(&walk, &part) == 1 && part.kind == PATH_PARENT && part.directory &&
              fspathnext(&walk, &part) == 1 && part.bytes == 4 && !memcmp(part.name, "file", 4) &&
              part.final && !part.directory && !fspathnext(&walk, &part),
          287);
  REQUIRE(!fspathinit(&walk, "./link//", sizeof("./link//")) && fspathnext(&walk, &part) == 1 &&
              part.kind == PATH_DOT && !part.final && fspathnext(&walk, &part) == 1 && part.final &&
              part.directory && !fspathexpand(&walk, "../name", sizeof("../name")) &&
              fspathnext(&walk, &part) == 1 && part.kind == PATH_PARENT &&
              fspathnext(&walk, &part) == 1 && part.final && part.directory &&
              !memcmp(part.name, "name", 4) && !fspathnext(&walk, &part),
          288);
  REQUIRE(fspathinit(&walk, "", 1) == -ENOENT && fspathinit(&walk, "a\0b", 4) == -EINVAL &&
              !fspathinit(&walk, "///", 4) && fspathnext(&walk, &part) == 1 &&
              part.kind == PATH_ROOT && part.final && !fspathnext(&walk, &part),
          289);
  memset(longpath, 'x', sizeof(longpath));
  longpath[sizeof(longpath) - 1] = 0;
  REQUIRE(!fspathinit(&walk, "link/tail", sizeof("link/tail")) && fspathnext(&walk, &part) == 1 &&
              fspathexpand(&walk, longpath, sizeof(longpath)) == -ENAMETOOLONG && !walk.links &&
              fspathnext(&walk, &part) == 1 && part.final && !memcmp(part.name, "tail", 4) &&
              !fspathinit(&walk, longpath, sizeof(longpath)) &&
              fspathnext(&walk, &part) == -ENAMETOOLONG,
          290);
  REQUIRE(!fspathinit(&walk, "link", 5), 291);
  for (i = 0; i < FS_PATH_LINKS; i++)
    REQUIRE(fspathnext(&walk, &part) == 1 && !fspathexpand(&walk, "link", 5), 292);
  REQUIRE(fspathnext(&walk, &part) == 1 && fspathexpand(&walk, "link", 5) == -ELOOP, 293);
}

static void __attribute__((noinline, noreturn))
jumpfrom(jmp_buf target, int value)
{
  volatile u64 stack[8];
  stack[0] = (u64) value;
  longjmp(target, (int) stack[0]);
}

static void
jumpcheck(void)
{
  jmp_buf      outer, inner;
  volatile int step = 0;
  switch (setjmp(outer)) {
  case 0:
    step = 1;
    switch (setjmp(inner)) {
    case 0:
      jumpfrom(inner, 0);
    case 1:
      REQUIRE(step == 1, 280);
      step = 2;
      jumpfrom(outer, -23);
    default:
      REQUIRE(0, 281);
    }
    break;
  case -23:
    REQUIRE(step == 2, 282);
    return;
  }
  REQUIRE(0, 283);
}

static void
localecheck(void)
{
  char        **saved = environ;
  char         *env[] = {"LC_ALL=", "LC_NUMERIC=POSIX", "LANG=unsupported", NULL};
  struct lconv *conventions = localeconv();
  REQUIRE(!strcmp(setlocale(LC_ALL, NULL), "C") && !strcmp(setlocale(LC_ALL, "POSIX"), "C") &&
              !setlocale(LC_ALL, "C.UTF-8") && !setlocale(-1, "C"),
          280);
  environ = env;
  REQUIRE(!strcmp(setlocale(LC_NUMERIC, ""), "C") && !setlocale(LC_CTYPE, "") &&
              !setlocale(LC_ALL, ""),
          281);
  env[0] = "LC_ALL=C";
  REQUIRE(!strcmp(setlocale(LC_ALL, ""), "C"), 282);
  env[0] = "LC_ALL=unsupported";
  REQUIRE(!setlocale(LC_NUMERIC, "") && !strcmp(setlocale(LC_NUMERIC, NULL), "C"), 283);
  environ = saved;
  REQUIRE(!strcmp(conventions->decimal_point, ".") && !*conventions->thousands_sep &&
              !*conventions->mon_grouping && conventions->frac_digits == CHAR_MAX &&
              conventions->int_n_sign_posn == CHAR_MAX,
          284);
}

static void
endiancheck(void)
{
  volatile u32 value = UINT32_C(0x01234567);
  u32          big = htobe32(value++);
  const u8    *bytes = (const u8 *) &big;
  u64          wide = UINT64_C(0x0123456789abcdef);
  REQUIRE(BYTE_ORDER == LITTLE_ENDIAN && value == UINT32_C(0x01234568) && bytes[0] == 1 &&
              bytes[1] == 0x23 && bytes[2] == 0x45 && bytes[3] == 0x67 &&
              be32toh(big) == UINT32_C(0x01234567),
          280);
  REQUIRE(bswap_16(0x1234) == 0x3412 && bswap_32(value) == UINT32_C(0x68452301) &&
              bswap_64(wide) == UINT64_C(0xefcdab8967452301) &&
              be16toh(htobe16(0x1234)) == 0x1234 && be64toh(htobe64(wide)) == wide &&
              le16toh(htole16(0x1234)) == 0x1234 && le32toh(htole32(value)) == value &&
              le64toh(htole64(wide)) == wide,
          281);
}

static void
optioncheck(void)
{
  char *args[] = {"tool", "-abvalue", "-c", "-argument", "--", "-a", NULL};
  char *bad[] = {"tool", "-xa", "-b", NULL};
  char *operand[] = {"tool", "-", "-a", NULL};
  REQUIRE(optind == 1 && opterr == 1 && getopt(6, args, "+ab:c:") == 'a' && optind == 1 &&
              !optarg && getopt(6, args, "+ab:c:") == 'b' && optind == 2 && optarg == args[1] + 3 &&
              getopt(6, args, "+ab:c:") == 'c' && optind == 4 && optarg == args[3] &&
              getopt(6, args, "+ab:c:") == -1 && optind == 5,
          280);
  optind = 1;
  REQUIRE(getopt(3, bad, "+:ab:") == '?' && optopt == 'x' && optind == 1 &&
              getopt(3, bad, "+:ab:") == 'a' && optind == 2 && getopt(3, bad, "+:ab:") == ':' &&
              optopt == 'b' && optind == 4 && !optarg,
          281);
  optind = 2;
  opterr = 0;
  REQUIRE(getopt(3, bad, "ab:") == '?' && optopt == 'b' && !optarg, 282);
  optind = 1;
  REQUIRE(getopt(3, operand, "a") == -1 && optind == 1, 283);
  operand[1] = "path";
  REQUIRE(getopt(3, operand, "a") == -1 && optind == 1 && operand[2][1] == 'a', 284);
}

static void
longoptioncheck(void)
{
  int                 flag = 0, index = -1;
  const struct option options[] = {{"verbose", no_argument, &flag, 9},
                                   {"value", required_argument, NULL, 'v'},
                                   {"color", optional_argument, NULL, 'c'},
                                   {"collect", no_argument, NULL, 'C'},
                                   {NULL, 0, NULL, 0}};
  char               *args[] = {"tool",  "left",        "--verb", "-ab",       "--value", "42",
                                "right", "--color=red", "--",     "--collect", NULL};
  char               *errors[] = {"tool", "--col", "--verbose=1", "--value", NULL};
  char               *operands[] = {"tool", "left", "-a", NULL};
  char               *optional[] = {"tool", "-cblue", "--color", "operand", "-c", NULL};
  optind = 0;
  REQUIRE(getopt_long(10, args, "ab", options, &index) == 0 && flag == 9 && index == 0 &&
              getopt_long(10, args, "ab", options, &index) == 'a' &&
              getopt_long(10, args, "ab", options, &index) == 'b' &&
              getopt_long(10, args, "ab", options, &index) == 'v' && index == 1 &&
              !strcmp(optarg, "42") && getopt_long(10, args, "ab", options, &index) == 'c' &&
              index == 2 && !strcmp(optarg, "red") &&
              getopt_long(10, args, "ab", options, NULL) == -1 && optind == 7 &&
              !strcmp(args[6], "--") && !strcmp(args[7], "left") && !strcmp(args[8], "right") &&
              !strcmp(args[9], "--collect"),
          280);
  optind = 0;
  REQUIRE(getopt_long(4, errors, ":", options, &index) == '?' && !optopt && index == 2 &&
              getopt_long(4, errors, ":", options, &index) == '?' && optopt == 9 &&
              getopt_long(4, errors, ":", options, &index) == ':' && optopt == 'v' && optind == 4,
          281);
  optind = 0;
  REQUIRE(getopt_long(3, operands, "+a", options, NULL) == -1 && optind == 1, 282);
  optind = 0;
  REQUIRE(getopt_long(3, operands, "-a", options, NULL) == 1 && optarg == operands[1] &&
              getopt_long(3, operands, "-a", options, NULL) == 'a',
          283);
  optind = 0;
  REQUIRE(getopt_long(5, optional, "c::", options, NULL) == 'c' && !strcmp(optarg, "blue") &&
              getopt_long(5, optional, "c::", options, NULL) == 'c' && !optarg &&
              getopt_long(5, optional, "c::", options, NULL) == 'c' && !optarg &&
              getopt_long(5, optional, "c::", options, NULL) == -1 && optind == 4 &&
              !strcmp(optional[4], "operand"),
          284);
}
volatile u64                             runtime_trace[18];
static volatile u64                      initialized = UINT64_C(0x123456789abcdef0);
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
    {" \t\n\v\f\r-0x2a!", 0, -42, ULONG_MAX - 41, 11, EDOM, EDOM},
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
STATIC_ASSERT(sizeof(time_t) == 8, runtime_time_t);
STATIC_ASSERT(sizeof(struct timespec) == 16, runtime_timespec_size);
STATIC_ASSERT(offsetof(struct timespec, tv_sec) == 0, runtime_timespec_seconds);
STATIC_ASSERT(offsetof(struct timespec, tv_nsec) == 8, runtime_timespec_nanoseconds);
static int
comparebyte(const void *a, const void *b)
{
  return *(const u8 *) a - *(const u8 *) b;
}

static void
sortcheck(void)
{
  u8       records[][3] = {{9, 0, 20}, {4, 1, 21}, {4, 2, 22}, {1, 3, 23}, {0, 4, 24}, {7, 5, 25}};
  const u8 original[] = {9, 4, 4, 1, 0, 7};
  const u8 sorted[] = {0, 1, 4, 4, 7, 9};
  u8       key, *match;
  u32      i, seen = 0;

  qsort(records, ARRAY_SIZE(records), sizeof(records[0]), comparebyte);
  for (i = 0; i < ARRAY_SIZE(records); i++) {
    u8 id = records[i][1];
    REQUIRE(records[i][0] == sorted[i] && id < ARRAY_SIZE(original) &&
                records[i][0] == original[id] && records[i][2] == id + 20,
            240);
    seen |= 1U << id;
  }
  REQUIRE(seen == 0x3f, 241);
  for (key = 0; key <= 10; key++) {
    match = bsearch(&key, records, ARRAY_SIZE(records), sizeof(records[0]), comparebyte);
    for (i = 0; i < ARRAY_SIZE(records) && sorted[i] != key; i++)
      ;
    REQUIRE(i == ARRAY_SIZE(records) ? !match : match && *match == key, 242);
  }
  REQUIRE(!bsearch(&key, records, 0, sizeof(records[0]), comparebyte), 243);
}

static u32
numbercheck(void)
{
  char       *end;
  char        formatted[64];
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
    errno = EDOM;
    REQUIRE(strtoll(test->text, &end, test->base) == test->signed_value &&
                end == test->text + test->end && errno == test->signed_error,
            180);
    errno = EDOM;
    REQUIRE(strtoull(test->text, &end, test->base) == test->unsigned_value &&
                end == test->text + test->end && errno == test->unsigned_error,
            181);
    errno = EDOM;
    REQUIRE(strtoimax(test->text, &end, test->base) == test->signed_value &&
                end == test->text + test->end && errno == test->signed_error,
            180);
    errno = EDOM;
    REQUIRE(strtoumax(test->text, &end, test->base) == test->unsigned_value &&
                end == test->text + test->end && errno == test->unsigned_error,
            181);
  }
  REQUIRE(snprintf(formatted, sizeof(formatted),
                   "%" PRId8 "/%" PRIXLEAST16 "/%" PRIuFAST32 "/%" PRIdMAX "/%" PRIxPTR,
                   (int8_t) -7, (uint_least16_t) 0xbeef, (uint_fast32_t) 4000000000U, INTMAX_MIN,
                   (uintptr_t) 0x123) ==
                  sizeof("-7/BEEF/4000000000/-9223372036854775808/123") - 1 &&
              !strcmp(formatted, "-7/BEEF/4000000000/-9223372036854775808/123"),
          182);
  for (base = 2; base <= 36; base++) {
    errno = EDOM;
    REQUIRE(strtol(text, &end, base) == base && end == text + 2 && errno == EDOM, 182);
    REQUIRE(strtoul(text, &end, base) == (unsigned long) base && end == text + 2 && errno == EDOM,
            183);
  }
  return (u32) ARRAY_SIZE(numbers) + 35;
}

static void
ctypecheck(void)
{
  static int (*const classify[])(int) = {isalnum, isalpha, isblank, iscntrl, isdigit, isgraph,
                                         islower, isprint, ispunct, isspace, isupper, isxdigit};
  static const u32 totals[] = {62, 52, 2, 33, 10, 94, 26, 95, 32, 6, 26, 22};
  u32              i, count;
  const int        eof = -1;
  int              byte;

  errno = EDOM;
  for (i = 0; i < ARRAY_SIZE(classify); i++) {
    REQUIRE(!classify[i](eof), 244);
    count = 0;
    for (byte = 0; byte <= UCHAR_MAX; byte++) {
      int member = classify[i](byte);
      REQUIRE(byte < 128 || !member, 244);
      count += !!member;
    }
    REQUIRE(count == totals[i], 244);
  }
  REQUIRE(isblank('\t') && !isblank('\n') && isspace('\r') && iscntrl(127) && isprint(' ') &&
              !isgraph(' ') && ispunct('_') && !isalpha('@') && isxdigit('A') && isxdigit('f') &&
              !isxdigit('g'),
          245);
  for (byte = 0; byte <= UCHAR_MAX; byte++) {
    int lower = tolower(byte), upper = toupper(byte);
    REQUIRE(lower == (isupper(byte) ? byte + 32 : byte) &&
                upper == (islower(byte) ? byte - 32 : byte),
            246);
  }
  REQUIRE(tolower(eof) == eof && toupper(eof) == eof && errno == EDOM, 247);
}
static void
collationcheck(void)
{
  char       key[] = "zzzz", other[] = "zzzz";
  const char prefix[] = {'I', 'N', 'F'};

  errno = EDOM;
  REQUIRE(strxfrm(NULL, "\377a", 0) == 2 && strxfrm(key, "\377a", 2) == 2 && !strcmp(key, "zzzz"),
          248);
  REQUIRE(strxfrm(key, "\377a", 3) == 2 && !memcmp(key, "\377a\0z", 4) &&
              strxfrm(other, "a", sizeof(other)) == 1,
          249);
  REQUIRE(strcoll("\377a", "a") > 0 && strcmp(key, other) > 0 && !strcoll(key, "\377a") &&
              strcoll("", "a") < 0,
          250);
  REQUIRE(strxfrm(other, "", 1) == 0 && !other[0] && errno == EDOM, 251);
  REQUIRE(!strcasecmp("sIgInT", "SIGINT") && strcasecmp("INT", "InTa") < 0 &&
              strcasecmp("\377a", "\200A") > 0,
          307);
  REQUIRE(!strncasecmp(prefix, "inf", sizeof(prefix)) && strncasecmp("in", "INF", 3) < 0 &&
              !strncasecmp("X", "Y", 0) && errno == EDOM,
          308);
}
static void
sigdescriptioncheck(void)
{
  const char *interrupt = strsignal(SIGINT);
  errno = EDOM;
  REQUIRE(!strcmp(strsignal(SIGTSTP), "Stopped") && !strcmp(strsignal(SIGTERM), "Terminated") &&
              !strcmp(interrupt, "Interrupt") && errno == EDOM,
          309);
  REQUIRE(!strcmp(strsignal(0), "Unknown signal") &&
              !strcmp(strsignal(INT_MIN), "Unknown signal") &&
              !strcmp(strsignal(INT_MAX), "Unknown signal"),
          310);
}
static void
libgencheck(void)
{
  const char *const cases[][3] = {
      {"", ".", "."},    {"name", ".", "name"},       {"//", "/", "/"},
      {"///", "/", "/"}, {"///name///", "/", "name"}, {"a//../b//", "a//..", "b"},
      {"a/b", "a", "b"}, {"a///", ".", "a"}};
  char  path[24];
  usize i;
  errno = EDOM;
  REQUIRE(!strcmp(dirname(NULL), ".") && !strcmp(basename(NULL), "."), 311);
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    strcpy(path, cases[i][0]);
    REQUIRE(!strcmp(dirname(path), cases[i][1]), 312);
    strcpy(path, cases[i][0]);
    REQUIRE(!strcmp(basename(path), cases[i][2]) && errno == EDOM, 313);
  }
}
static void
patterncheck(void)
{
  REQUIRE(!fnmatch("a**b?c*d", "abbxcyd", 0) && fnmatch("a**b?c*d", "abxcye", 0) == FNM_NOMATCH &&
              !fnmatch("*", "", 0) && fnmatch("?", "", 0) == FNM_NOMATCH,
          248);
  REQUIRE(!fnmatch("a/*/b", "a/x/b", FNM_PATHNAME) &&
              fnmatch("a/*/b", "a/x/y/b", FNM_PATHNAME) == FNM_NOMATCH &&
              !fnmatch("a/*/b", "a/x/y/b", 0) &&
              fnmatch("a[/]b", "a/b", FNM_PATHNAME) == FNM_NOMATCH,
          249);
  REQUIRE(fnmatch("a/*", "a/.x", FNM_PATHNAME | FNM_PERIOD) == FNM_NOMATCH &&
              fnmatch("[.]x", ".x", FNM_PERIOD) == FNM_NOMATCH &&
              !fnmatch("a/.*", "a/.x", FNM_PATHNAME | FNM_PERIOD) &&
              !fnmatch("a/*", "a/.x", FNM_PERIOD),
          250);
  REQUIRE(!fnmatch("[!a-c][]-][[:digit:]]", "z]4", 0) &&
              fnmatch("[!a-c][]-][[:digit:]]", "b-4", 0) == FNM_NOMATCH &&
              !fnmatch("[[.a.]-[.c.]][[=z=]]", "bz", 0) && !fnmatch("[\200-\377]", "\377", 0),
          251);
  REQUIRE(!fnmatch("\\*\\?", "*?", 0) && fnmatch("\\*\\?", "*?", FNM_NOESCAPE) == FNM_NOMATCH &&
              !fnmatch("[abc", "[abc", 0) && fnmatch("\\", "\\", 0) == FNM_NOMATCH &&
              !fnmatch("\\", "\\", FNM_NOESCAPE),
          252);
}
static void
tokencheck(void)
{
  char  first[] = ",,one,,two;three;", second[] = "\377a\377b", plain[] = "x:y", empty[] = ";;";
  char *a, *b;

  errno = EDOM;
  REQUIRE(strtok_r(first, ",", &a) == first + 2 && !strcmp(first + 2, "one") && first[0] == ',',
          248);
  REQUIRE(strtok_r(second, "\377", &b) == second + 1 && !strcmp(second + 1, "a"), 249);
  REQUIRE(strtok(plain, ":") == plain && !strcmp(plain, "x"), 250);
  REQUIRE(strtok_r(NULL, ",;", &a) == first + 7 && !strcmp(first + 7, "two"), 251);
  REQUIRE(strtok_r(NULL, "", &b) == second + 3 && !strcmp(second + 3, "b") &&
              !strtok_r(NULL, "\377", &b),
          252);
  REQUIRE(strtok(NULL, ":") == plain + 2 && !strtok(NULL, ":"), 253);
  REQUIRE(strtok_r(NULL, ";", &a) == first + 11 && !strcmp(first + 11, "three") &&
              !strtok_r(NULL, ";", &a) && !strtok_r(NULL, ";", &a),
          254);
  REQUIRE(!strtok(empty, ";") && !strtok(NULL, "") && errno == EDOM, 255);
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
static u32
timecheck(void)
{
  struct timespec monotonic, later, realtime, resolution, rt_resolution, before, invalid;
  u32             checks = 0;
#define TCHECK(c, n)                                                                               \
  do {                                                                                             \
    REQUIRE(c, n);                                                                                 \
    checks++;                                                                                      \
  } while (0)
  errno = EDOM;
  TCHECK(!clock_gettime(CLOCK_MONOTONIC, &monotonic) && monotonic.tv_sec >= 0 &&
             monotonic.tv_nsec >= 0 && monotonic.tv_nsec < 1000000000 && errno == EDOM,
         256);
  TCHECK(!clock_gettime(CLOCK_MONOTONIC, &later) &&
             (later.tv_sec > monotonic.tv_sec ||
              (later.tv_sec == monotonic.tv_sec && later.tv_nsec >= monotonic.tv_nsec)) &&
             errno == EDOM,
         257);
  TCHECK(!clock_gettime(CLOCK_REALTIME, &realtime) && realtime.tv_sec >= 0 &&
             realtime.tv_nsec >= 0 && realtime.tv_nsec < 1000000000 && errno == EDOM,
         258);
  TCHECK(!clock_getres(CLOCK_MONOTONIC, &resolution) && resolution.tv_sec >= 0 &&
             resolution.tv_nsec >= 0 && (resolution.tv_sec || resolution.tv_nsec) && errno == EDOM,
         259);
  TCHECK(!clock_getres(CLOCK_REALTIME, &rt_resolution) &&
             rt_resolution.tv_sec == resolution.tv_sec &&
             rt_resolution.tv_nsec == resolution.tv_nsec && errno == EDOM,
         260);
  TCHECK(clock_getres(CLOCK_MONOTONIC, NULL) == 0 && errno == EDOM, 261);
  TCHECK(clock_gettime(0, &invalid) == -1 && errno == EINVAL, 262);
  before.tv_sec = before.tv_nsec = 77;
  TCHECK(clock_gettime(CLOCK_MONOTONIC, NULL) == -1 && errno == EFAULT, 263);
  TCHECK(clock_getres(0, NULL) == -1 && errno == EINVAL, 264);
  errno = EDOM;
  TCHECK(!clock_gettime(CLOCK_REALTIME, &before) && errno == EDOM && before.tv_nsec < 1000000000,
         265);
#undef TCHECK
  {
    struct timeval wall;
    time_t         seconds = -1, value;
    REQUIRE(!gettimeofday(&wall, NULL) && wall.tv_usec >= 0 && wall.tv_usec < 1000000 &&
                wall.tv_sec >= before.tv_sec && errno == EDOM,
            317);
    value = time(&seconds);
    REQUIRE(value == seconds && value >= wall.tv_sec && time(NULL) >= value && errno == EDOM, 318);
    REQUIRE(gettimeofday(&wall, &seconds) == -1 && errno == EINVAL &&
                gettimeofday(NULL, NULL) == -1 && errno == EFAULT &&
                time((time_t *) (uintptr_t) 1) == -1 && errno == EFAULT,
            319);
  }
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
static void
timercheck(void)
{
  Timerheap heap = {0};
  Timerjob  due = {0, 0};
  Ipcmsg    event;
  u64       now, deadline, old, next, equal, late, generation;
  u64       tickets[SYSTIMER_LIMIT];
  u32       i;

  REQUIRE(!systimernow(&now) && now < UINT64_MAX - UINT64_C(2000000000), 101);
  deadline = now + UINT64_C(25000000);
  REQUIRE(!systimeradd(&heap, now, &old) && heap.generation, 102);
  generation = heap.generation;
  REQUIRE(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == EVENT_TIMER && event.words[1] == generation,
          103);
  REQUIRE(!systimeradd(&heap, deadline + UINT64_C(1000000000), &late) &&
              !systimeradd(&heap, deadline, &next) && !systimeradd(&heap, deadline, &equal) &&
              heap.generation == generation && !systimercancel(&heap, old) &&
              heap.generation != event.words[1] && systimercancel(&heap, old) == -ERR_STALE,
          104);
  REQUIRE(systimerdue(&heap, deadline - 1, &due) == -ERR_WOULD_BLOCK && !due.ticket, 105);
  generation = heap.generation;
  REQUIRE(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == EVENT_TIMER && event.words[1] == generation && !systimernow(&now) &&
              now >= deadline,
          106);
  REQUIRE(!systimerdue(&heap, now, &due) && due.ticket == next && due.deadline == deadline &&
              heap.generation == generation && !systimerdue(&heap, now, &due) &&
              due.ticket == equal && due.deadline == deadline && heap.generation != generation &&
              !systimercancel(&heap, late),
          107);

  REQUIRE(now < UINT64_MAX - UINT64_C(10000000000) - SYSTIMER_LIMIT, 108);
  deadline = now + UINT64_C(10000000000);
  for (i = 0; i < SYSTIMER_LIMIT; i++)
    REQUIRE(!systimeradd(&heap, deadline + SYSTIMER_LIMIT - i, &tickets[i]), 109);
  generation = heap.generation;
  REQUIRE(systimeradd(&heap, now, &old) == -ERR_CAPACITY && heap.generation == generation &&
              !systimercancel(&heap, tickets[5]) && heap.generation == generation,
          110);
  for (i = SYSTIMER_LIMIT; i > 0; i--)
    if (i - 1 != 5)
      REQUIRE(!systimerdue(&heap, deadline + SYSTIMER_LIMIT, &due) && due.ticket == tickets[i - 1],
              111);
  REQUIRE(!heap.count && systimerdue(&heap, deadline, &due) == -ERR_WOULD_BLOCK &&
              ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY | RECV_NONBLOCK, &event) == -ERR_WOULD_BLOCK,
          112);
}

static volatile u32 slept;
static volatile u32 continued;

static void
sleepcontinue(int signal)
{
  REQUIRE(signal == SIGCONT, 122);
  continued++;
}

static void
sleepinterrupt(int signal)
{
  const struct timespec nested = {0, 300000000};
  REQUIRE(signal == SIGUSR1, 113);
  if (!slept)
    REQUIRE(!nanosleep(&nested, NULL), 114);
  slept++;
}

static void
sleepcheck(void)
{
  const struct timespec brief = {0, 20000000}, invalid = {0, 1000000000};
  struct timespec       request = {0, 250000000}, remaining = {4, 9};
  struct sigaction      action = {0};
  u64                   before, after;
  errno = EDOM;
  REQUIRE(!systimernow(&before) && !nanosleep(&brief, &remaining) && !systimernow(&after) &&
              after - before >= (u64) brief.tv_nsec && remaining.tv_sec == 4 &&
              remaining.tv_nsec == 9 && errno == EDOM,
          115);
  REQUIRE(nanosleep(&invalid, &remaining) == -1 && errno == EINVAL, 116);
  REQUIRE(nanosleep(NULL, &remaining) == -1 && errno == EFAULT, 117);
  action.sa_handler = sleepinterrupt;
  action.sa_flags = SA_RESTART;
  REQUIRE(!sigaction(SIGUSR1, &action, NULL), 118);
  runtime_trace[6] = 91;
  REQUIRE(nanosleep(&request, &request) == -1 && errno == EINTR && slept == 1 && !request.tv_sec &&
              request.tv_nsec > 0 && request.tv_nsec <= 250000000,
          119);
  request.tv_nsec = 250000000;
  runtime_trace[6] = 92;
  REQUIRE(nanosleep(&request, (struct timespec *) (uintptr_t) 1) == -1 && errno == EFAULT &&
              slept == 2,
          120);
  /* The scheduler stops this process only after observing the native wait. */
  runtime_trace[6] = 93;
  REQUIRE(!nanosleep(&brief, &remaining) && !continued && remaining.tv_sec == 4 &&
              remaining.tv_nsec == 9,
          121);
  action.sa_handler = sleepcontinue;
  REQUIRE(!sigaction(SIGCONT, &action, NULL), 123);
  request.tv_nsec = 900000000;
  runtime_trace[6] = 94;
  REQUIRE(nanosleep(&request, &remaining) == -1 && errno == EINTR && continued == 1 &&
              !remaining.tv_sec && remaining.tv_nsec > 0 && remaining.tv_nsec <= request.tv_nsec,
          124);
  before = (u64) remaining.tv_nsec;
  runtime_trace[6] = 95;
  REQUIRE(!nanosleep(&brief, &remaining) && continued == 2 && !remaining.tv_sec &&
              (u64) remaining.tv_nsec == before,
          125);
  errno = EDOM;
  REQUIRE(!sleep(0) && errno == EDOM, 314);
  runtime_trace[6] = 96;
  REQUIRE(sleep(2) == 2 && slept == 3 && errno == EDOM, 315);
  runtime_trace[6] = 0;
  REQUIRE(!systimernow(&before) && !sleep(1) && !systimernow(&after) &&
              after - before >= UINT64_C(1000000000) && errno == EDOM,
          316);
}

static void
pipecheck(void)
{
  static Pipe pipe;
  static u8   first[PIPE_CAPACITY], second[PIPE_ATOMIC], output[PIPE_CAPACITY];
  u32         i;
  for (i = 0; i < sizeof(first); i++)
    first[i] = (u8) (i * 17 + (i >> 8));
  memset(second, 0x9e, sizeof(second));
  pipeinit(&pipe);
  REQUIRE(!pipehold(&pipe, PIPE_WRITE) && !pipedrop(&pipe, PIPE_WRITE) &&
              piperead(&pipe, output, sizeof(output)) == -ERR_WOULD_BLOCK,
          126);
  REQUIRE(pipewrite(&pipe, first, PIPE_ATOMIC + 1) == PIPE_ATOMIC + 1 &&
              pipewrite(&pipe, second, sizeof(second)) == -ERR_WOULD_BLOCK &&
              piperead(&pipe, output, 2) == 2 && !memcmp(output, first, 2) &&
              pipewrite(&pipe, second, sizeof(second)) == sizeof(second),
          127);
  REQUIRE(pipewrite(&pipe, first, sizeof(first)) == 1 && !pipedrop(&pipe, PIPE_WRITE) &&
              pipehold(&pipe, PIPE_WRITE) == -ERR_STALE &&
              piperead(&pipe, output, sizeof(output)) == sizeof(output) &&
              !memcmp(output, first + 2, PIPE_ATOMIC - 1) &&
              !memcmp(output + PIPE_ATOMIC - 1, second, sizeof(second)) &&
              output[PIPE_CAPACITY - 1] == first[0] && !piperead(&pipe, output, sizeof(output)),
          128);
  REQUIRE(!pipedrop(&pipe, PIPE_READ), 129);
  pipeinit(&pipe);
  REQUIRE(!pipehold(&pipe, PIPE_READ) && !pipedrop(&pipe, PIPE_READ) &&
              pipewrite(&pipe, first, sizeof(first)) == sizeof(first) &&
              !pipedrop(&pipe, PIPE_READ) && pipehold(&pipe, PIPE_READ) == -ERR_STALE &&
              pipewrite(&pipe, second, sizeof(second)) == -ERR_PEER && !pipedrop(&pipe, PIPE_WRITE),
          130);
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
  if (mode && !strcmp(mode, "4")) {
    Queryrec entries[2];
    Handle   pid, parent, space, allocator;
    u64      image, count, next;
    REQUIRE(argc == 3 && !strcmp(argv[0], "runtime") && !strcmp(argv[1], "alpha") &&
                !strcmp(argv[2], "beta") && getenv("X") && !strcmp(getenv("X"), "value"),
            268);
    REQUIRE(!__sys_getid(&pid, &image, &parent) && pid == getpid() && pid > 1 && parent == 1 &&
                image == 3,
            269);
    space = bootresource(&view, BOOT_SELF_SPACE);
    allocator = bootresource(&view, BOOT_SELF_ALLOC);
    REQUIRE(space > 0 && allocator > 0 && !bootresource(&view, BOOT_CREATE) &&
                !__sys_query(0, QUERY_RESOURCE, 0, entries, 2, &count, &next) && count == 2 &&
                entries[0].image == image && entries[1].image == image &&
                (Handle) entries[0].data[0] == space && (Handle) entries[1].data[0] == allocator,
            270);
    _exit(42);
  }
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
  {
    const char text[] = "a\377b";
    REQUIRE(strchrnul(text, -1) == text + 1 && strchrnul(text, 'z') == text + 3 &&
                strchrnul(text, 0) == text + 3 && strchrnul(text + 3, 'a') == text + 3,
            176);
  }
  sortcheck();
  optioncheck();
  jumpcheck();
  endiancheck();
  localecheck();
  longoptioncheck();
  pathwalkcheck();
  REQUIRE(!vwalkusercheck(), 294);
  ctypecheck();
  collationcheck();
  sigdescriptioncheck();
  libgencheck();
  patterncheck();
  tokencheck();
  runtime_trace[8] = numbercheck();
  runtime_trace[9] = formatcheck();
  {
    char *text = NULL;
    errno = EDOM;
    REQUIRE(asprintf(&text, "%s:%03d", "ash", 7) == 7 && !strcmp(text, "ash:007") && errno == EDOM,
            190);
    free(text);
    REQUIRE(asprintf(&text, "%.*s", 0, "ash") == 0 && !*text && errno == EDOM, 190);
    free(text);
    text = NULL;
    REQUIRE(asprintf(&text, "%f", 1.0) == -1 && !text && errno == EINVAL, 190);
  }
  runtime_trace[10] = groupcheck();
  runtime_trace[11] = queuecheck();
  runtime_trace[12] = ringusercheck();
  runtime_trace[13] = fdtableusercheck();
  runtime_trace[14] = fileusercheck();
  runtime_trace[15] = stringusercheck(&view);
  runtime_trace[16] = envusercheck(&view);
  runtime_trace[17] = timecheck();
  if (runtime_trace[4] == 1) {
    spawncheck();
    spawnchildcheck();
    timercheck();
    sleepcheck();
    pipecheck();
  }
  errno = EDOM;
  REQUIRE(sysconf(_SC_PAGESIZE) == PAGE_SIZE && sysconf(_SC_PAGE_SIZE) == PAGE_SIZE &&
              errno == EDOM,
          266);
  REQUIRE(sysconf(-1) == -1 && errno == EINVAL, 267);
  errno = EDOM;
  runtime_trace[6] = 42;
  if (runtime_trace[4] == 2)
    exit(0x12a);
  if (runtime_trace[4] == 3)
    _exit(0x12a);
  return 42;
}
