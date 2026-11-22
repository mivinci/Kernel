#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int   opterr = 1;
int   optind = 1;
int   optopt;

static void
diagnostic(const char *text, size_t bytes)
{
  while (bytes) {
    ssize_t written = write(2, text, bytes);
    if (written == -1 && errno == EINTR)
      continue;
    if (written <= 0)
      return;
    text += written;
    bytes -= (size_t) written;
  }
}

int
getopt(int argc, char *const argv[], const char *optstring)
{
  static char *const *previous;
  static int          index;
  static char        *next;
  const char         *spec;
  unsigned char       option;
  int                 missing = 0;

  optarg = NULL;
  if (previous != argv || optind != index)
    next = NULL;
  if (optind < 1 || optind >= argc || !argv[optind])
    return -1;
  if (!next) {
    char *argument = argv[optind];
    if (argument[0] != '-' || !argument[1])
      return -1;
    if (argument[1] == '-' && !argument[2]) {
      optind++;
      return -1;
    }
    previous = argv;
    index = optind;
    next = argument + 1;
  }
  if (*optstring == '+')
    optstring++;
  option = (unsigned char) *next++;
  spec = option == ':' ? NULL : strchr(optstring, option);
  if (spec && spec[1] == ':') {
    if (*next)
      optarg = next;
    else if (optind + 1 < argc && argv[optind + 1])
      optarg = argv[++optind];
    else {
      missing = 1;
      /* Skip the missing argument position as well as the option. */
      if (optind < INT_MAX - 1)
        optind++;
    }
    next = NULL;
  } else if (!*next)
    next = NULL;
  if (!next)
    optind++;
  if (spec && !missing)
    return option;
  optopt = option;
  if (opterr && *optstring != ':') {
    const char *name = argv[0] ? argv[0] : "";
    const char *message = missing ? ": option requires an argument -- " : ": illegal option -- ";
    char        tail[] = {(char) option, '\n'};
    diagnostic(name, strlen(name));
    diagnostic(message, strlen(message));
    diagnostic(tail, sizeof(tail));
  }
  return missing && *optstring == ':' ? ':' : '?';
}
