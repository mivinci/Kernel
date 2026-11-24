#include <errno.h>
#include <sys/wait.h>

extern long syswaitposix(int, int *, unsigned int);

pid_t
waitpid(pid_t pid, int *status, int options)
{
  long result;
  if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) {
    errno = EINVAL;
    return -1;
  }
  result = syswaitposix(pid, status,
                        (options & (WNOHANG | WUNTRACED)) | (options & WCONTINUED ? 4u : 0));
  if (result < 0) {
    errno = (int) -result;
    return -1;
  }
  return (pid_t) result;
}
