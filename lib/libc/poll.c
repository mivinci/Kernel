#include <errno.h>
#include <poll.h>
#include "private/fileimage.h"
#include "private/syscall.h"
#include "sigdepth.h"

STATIC_ASSERT(sizeof(struct pollfd) == sizeof(Pollfd) &&
                  offsetof(struct pollfd, revents) == offsetof(Pollfd, revents),
              pollfd_layout);
STATIC_ASSERT(POLLIN == FS_POLL_IN && POLLPRI == FS_POLL_PRI && POLLOUT == FS_POLL_OUT &&
                  POLLERR == FS_POLL_ERR && POLLHUP == FS_POLL_HUP && POLLNVAL == FS_POLL_NVAL,
              poll_events);

int
poll(struct pollfd *fds, nfds_t count, int timeout)
{
  Pollfd local[FS_POLL_ROWS];
  u64    base = (uintptr_t) fds;
  nfds_t i;
  isize  status;
  if (count > FS_POLL_ROWS) {
    errno = EINVAL;
    return -1;
  }
  if (count &&
      (base > UINT64_MAX - count * sizeof(*fds) || __sys_copy(local, fds, count * sizeof(*fds)))) {
    errno = EFAULT;
    return -1;
  }
  status = sysfileimagepoll(__libc_sigdepth(), local, (u32) count, timeout);
  if (status >= 0) {
    for (i = 0; i < count; i++) {
      void *output =
          (void *) (uintptr_t) (base + i * sizeof(*fds) + offsetof(struct pollfd, revents));
      if (__sys_copy(output, &local[i].revents, sizeof(local[i].revents))) {
        errno = EFAULT;
        return -1;
      }
    }
    return (int) status;
  }
  switch (status) {
  case -ERR_INTERRUPTED:
    errno = EINTR;
    break;
  case -ERR_BUSY:
  case -ERR_CAPACITY:
  case -ERR_LIMIT:
    errno = EAGAIN;
    break;
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  default:
    errno = EIO;
    break;
  }
  return -1;
}
