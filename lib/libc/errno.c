#include <errno.h>
#include <string.h>
static int error_number;
int *
__errno_location(void)
{
  return &error_number;
}
char *
strerror(int error)
{
  switch (error) {
  case 0:
    return "Success";
  case EPERM:
    return "Operation not permitted";
  case ENOENT:
    return "No such file or directory";
  case ESRCH:
    return "No such process";
  case EINTR:
    return "Interrupted operation";
  case EIO:
    return "Input/output error";
  case ENOEXEC:
    return "Executable format error";
  case E2BIG:
    return "Argument list too long";
  case EBADF:
    return "Bad file descriptor";
  case ECHILD:
    return "No child processes";
  case EAGAIN:
    return "Resource temporarily unavailable";
  case ENOMEM:
    return "Not enough memory";
  case EACCES:
    return "Permission denied";
  case EFAULT:
    return "Bad address";
  case EBUSY:
    return "Resource busy";
  case EINVAL:
    return "Invalid argument";
  case EMFILE:
    return "Too many open files";
  case EPIPE:
    return "Broken pipe";
  case ENOSYS:
    return "Function not implemented";
  case EOVERFLOW:
    return "Value too large";
  case ENOTSUP:
    return "Operation not supported";
  case ETIMEDOUT:
    return "Operation timed out";
  case ESTALE:
    return "Stale handle";
  case ECANCELED:
    return "Operation canceled";
  case ENXIO:
    return "No such device or address";
  case EEXIST:
    return "File exists";
  case EXDEV:
    return "Cross-device operation";
  case ENODEV:
    return "No such device";
  case ENOTDIR:
    return "Not a directory";
  case EISDIR:
    return "Is a directory";
  case ENFILE:
    return "Too many open files in system";
  case ENOTTY:
    return "Inappropriate device operation";
  case ETXTBSY:
    return "Executable file busy";
  case EFBIG:
    return "File too large";
  case ENOSPC:
    return "No space left on device";
  case ESPIPE:
    return "Invalid seek";
  case EROFS:
    return "Read-only file system";
  case EMLINK:
    return "Too many links";
  case EDOM:
    return "Argument outside function domain";
  case ERANGE:
    return "Result outside representable range";
  case EDEADLK:
    return "Deadlock avoided";
  case ENAMETOOLONG:
    return "File name too long";
  case ENOLCK:
    return "No locks available";
  case ENOTEMPTY:
    return "Directory not empty";
  case ELOOP:
    return "Too many symbolic links";
  case ENOTSOCK:
    return "Not a socket";
  case EDESTADDRREQ:
    return "Destination address required";
  case EMSGSIZE:
    return "Message too large";
  case EPROTOTYPE:
    return "Wrong protocol type";
  case ENOPROTOOPT:
    return "Protocol option unavailable";
  case EPROTONOSUPPORT:
    return "Protocol not supported";
  case EAFNOSUPPORT:
    return "Address family not supported";
  case EADDRINUSE:
    return "Address already in use";
  case EADDRNOTAVAIL:
    return "Address not available";
  case ENETDOWN:
    return "Network is down";
  case ENETUNREACH:
    return "Network unreachable";
  case ENETRESET:
    return "Network connection reset";
  case ECONNABORTED:
    return "Connection aborted";
  case ECONNRESET:
    return "Connection reset";
  case ENOBUFS:
    return "No buffer space available";
  case EISCONN:
    return "Socket already connected";
  case ENOTCONN:
    return "Socket not connected";
  case ECONNREFUSED:
    return "Connection refused";
  case EHOSTUNREACH:
    return "Host unreachable";
  case EALREADY:
    return "Operation already in progress";
  case EINPROGRESS:
    return "Operation in progress";
  default:
    return "Unknown error";
  }
}
