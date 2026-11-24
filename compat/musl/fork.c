#include <unistd.h>
#include "pthread_impl.h"
#include "fork_impl.h"
#include "lock.h"

extern long sysfileforkposix(void);
extern long __sys_getid(long *, unsigned long *, long *);
extern long __syscall_ret(unsigned long);
extern void __aio_atfork(int) __attribute__((__weak__));

/* musl's public fork still owns atfork callbacks and its outer locks. */
void
__post_Fork(int result)
{
  if (!result) {
    pthread_t self = __pthread_self();
    long pid, parent;
    unsigned long image;
    if (__sys_getid(&pid, &image, &parent))
      __builtin_trap();
    self->tid = pid;
    self->robust_list.off = 0;
    self->robust_list.pending = 0;
    self->next = self->prev = self;
    __thread_list_lock = 0;
    libc.threads_minus_1 = 0;
    if (libc.need_locks)
      libc.need_locks = -1;
  }
  UNLOCK(__abort_lock);
  if (!result && __aio_atfork)
    __aio_atfork(1);
}

pid_t
_Fork(void)
{
  sigset_t saved;
  long result;
  __block_all_sigs(&saved);
  LOCK(__abort_lock);
  result = sysfileforkposix();
  __post_Fork((int) result);
  __restore_sigs(&saved);
  return (pid_t) __syscall_ret(result);
}
