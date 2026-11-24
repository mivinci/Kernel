#include "private/syscall.h"
#include <signal.h>
#include <stdlib.h>

NORETURN void
abort(void)
{
  const u64 mask = UINT64_C(1) << (SIGNAL_ABRT - 1);
  Sigact    action = {0};
  u64       ignored;
  (void) __sys_sigmask(SIGNAL_UNBLOCK, mask, &ignored);
  (void) raise(SIGABRT);
  /* An ignored signal or a handler that returns cannot suppress abort. */
  action.kind = SIGNAL_DEFAULT;
  (void) __sys_sigaction(SIGNAL_ABRT, &action, NULL);
  (void) __sys_sigmask(SIGNAL_UNBLOCK, mask, &ignored);
  (void) raise(SIGABRT);
  __sys_exit(128 + SIGABRT);
}
