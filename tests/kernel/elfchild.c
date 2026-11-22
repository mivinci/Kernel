#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/boot.h>
#include <unistd.h>
#include "private/start.h"
#include "private/syscall.h"

#define NDEBUG
#include <assert.h>
static int
disabledassert(void)
{
  int evaluated = 0;
  assert(++evaluated);
  return evaluated;
}
#undef NDEBUG
#include <assert.h>

static volatile u64 initialized = UINT64_C(0x123456789abcdef0);
static volatile u32 constructed;

static void __attribute__((constructor))
initialize(void)
{
  if (!constructed && initialized == UINT64_C(0x123456789abcdef0) && getenv("MODE"))
    constructed = 1;
}

int
main(int argc, char **argv)
{
  const Bootview *view = sysboot();
  const char     *mode = getenv("MODE");
  char           *value = getenv("X");
  Queryrec        entries[3];
  Sigact          action;
  Handle          pid, parent, space, allocator, create;
  u64             image, count, next, mask;
  int             replacing, override;
  if (!constructed || !view || !mode || !value || argc != 3 || !argv || argv[3] ||
      strcmp(argv[0], "runtime") || strcmp(argv[1], "alpha") || strcmp(argv[2], "beta") ||
      strcmp(value, "value"))
    return 163;
  if (!strcmp(mode, "5")) {
    struct sigaction ignored = {0};
    sigset_t         blocked;
    int              evaluated = 0;
    ignored.sa_handler = SIG_IGN;
    assert(++evaluated == 1);
    if (evaluated != 1 || disabledassert() || sigemptyset(&blocked) ||
        sigaddset(&blocked, SIGABRT) || sigprocmask(SIG_BLOCK, &blocked, NULL) ||
        sigaction(SIGABRT, &ignored, NULL))
      return 172;
    assert(evaluated == 0);
    return 173;
  }
  replacing = !strcmp(mode, "4");
  override = !strcmp(mode, "1");
  if ((!replacing && !override && strcmp(mode, "2")) || __sys_getid(&pid, &image, &parent) ||
      pid != getpid() || pid <= 1 || image != (replacing ? 3U : 1U) ||
      (replacing ? parent != 1 : parent <= 1))
    return 169;
  if (!replacing && (__sys_sigaction(SIGNAL_USR1, NULL, &action) ||
                     action.kind != (override ? SIGNAL_DEFAULT : SIGNAL_IGNORE) ||
                     __sys_sigmask(SIGNAL_BLOCK, 0, &mask) ||
                     mask != (UINT64_C(1) << ((override ? SIGNAL_USR2 : SIGNAL_USR1) - 1))))
    return 171;
  space = bootresource(view, BOOT_SELF_SPACE);
  allocator = bootresource(view, BOOT_SELF_ALLOC);
  create = bootresource(view, BOOT_CREATE);
  if (space <= 0 || allocator <= 0 || (replacing ? create != 0 : create <= 0) ||
      __sys_query(0, QUERY_RESOURCE, 0, entries, replacing ? 2 : 3, &count, &next) ||
      count != (replacing ? 2U : 3U) || entries[0].image != image || entries[1].image != image ||
      (Handle) entries[0].data[0] != space || (Handle) entries[1].data[0] != allocator ||
      (!replacing && (next || entries[2].image != image || (Handle) entries[2].data[0] != create)))
    return 170;
  argv[1][0] = 'A';
  value[0] = 'V';
  if (strcmp(argv[1], "Alpha") || strcmp(getenv("X"), "Value") ||
      strcmp(bootarg(view, 1, 0), "alpha") || strcmp(bootarg(view, 1, 1), "X=value"))
    return 165;
  return 42;
}
