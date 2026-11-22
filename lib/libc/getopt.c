#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int   opterr = 1;
int   optind = 1;
int   optopt;

enum
{
  Stop,
  Permute,
  ReturnOperand
};

static char *const *previous;
static char        *next;
static int          index, extended, order, first, last, ended;

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

static void
reverse(char **argv, int low, int high)
{
  while (low < --high) {
    char *arg = argv[low];
    argv[low++] = argv[high];
    argv[high] = arg;
  }
}

static void
exchange(char **argv)
{
  /* Rotate the consumed options (including their arguments) ahead of the
     pending operands, preserving the order within each group. */
  if (first < last && last < optind) {
    reverse(argv, first, last);
    reverse(argv, last, optind);
    reverse(argv, first, optind);
    first += optind - last;
  } else if (last == first)
    first = optind;
  last = optind;
}

static int
longoption(int argc, char *const argv[], const char *spec, const struct option *options,
           int *position)
{
  const char *argument = argv[optind++], *name = argument + 2, *equal = strchr(name, '=');
  size_t      length = equal ? (size_t) (equal - name) : strlen(name);
  int         i, match = -1, ambiguous = 0, missing = 0;
  const char *problem = ": unrecognized option ";
  for (i = 0; options && length && options[i].name; i++) {
    if (strncmp(options[i].name, name, length))
      continue;
    if (!options[i].name[length]) {
      match = i;
      ambiguous = 0;
      break;
    }
    if (match < 0)
      match = i;
    else if (options[match].has_arg != options[i].has_arg ||
             options[match].flag != options[i].flag || options[match].val != options[i].val)
      ambiguous = 1;
  }
  optopt = 0;
  if (ambiguous)
    problem = ": option is ambiguous: ";
  else if (match >= 0) {
    const struct option *option = &options[match];
    if (equal && option->has_arg == no_argument)
      problem = ": option does not allow an argument: ";
    else if (!equal && option->has_arg == required_argument && (optind >= argc || !argv[optind])) {
      problem = ": option requires an argument: ";
      missing = 1;
    } else {
      if (equal)
        optarg = (char *) equal + 1;
      else if (option->has_arg == required_argument)
        optarg = argv[optind++];
      if (position)
        *position = match;
      if (!option->flag)
        return option->val;
      *option->flag = option->val;
      return 0;
    }
    optopt = option->val;
  }
  if (opterr && *spec != ':') {
    const char *program = argv[0] ? argv[0] : "";
    diagnostic(program, strlen(program));
    diagnostic(problem, strlen(problem));
    diagnostic(argument, strlen(argument));
    diagnostic("\n", 1);
  }
  return missing && *spec == ':' ? ':' : '?';
}

static int
parse(int argc, char *const argv[], const char *optstring, int longmode,
      const struct option *options, int *position)
{
  const char   *spec;
  unsigned char option;
  int           missing = 0;

  optarg = NULL;
  if (longmode && !optind) {
    optind = 1;
    index = 0;
  }
  if (previous != argv || optind != index || extended != longmode) {
    next = NULL;
    previous = argv;
    extended = longmode;
    first = last = optind;
    ended = 0;
    order = Stop;
    if (longmode && *optstring == '-')
      order = ReturnOperand;
    else if (longmode && *optstring != '+' && !getenv("POSIXLY_CORRECT"))
      order = Permute;
  }
  if (*optstring == '+' || (longmode && *optstring == '-'))
    optstring++;
  if (ended || optind < 1)
    return -1;
  if (!next) {
    char *argument;
    if (order == Permute) {
      exchange((char **) argv);
      while (optind < argc && argv[optind] && (argv[optind][0] != '-' || !argv[optind][1]))
        optind++;
      last = optind;
    }
    if (optind >= argc || !argv[optind]) {
      optind = order == Permute ? first : optind;
      ended = longmode;
      return -1;
    }
    argument = argv[optind];
    if (argument[0] != '-' || !argument[1]) {
      if (order != ReturnOperand)
        return -1;
      optarg = argv[optind++];
      return 1;
    }
    if (argument[1] == '-' && !argument[2]) {
      optind++;
      if (order == Permute) {
        exchange((char **) argv);
        optind = first;
      }
      ended = longmode;
      return -1;
    }
    if (longmode && argument[1] == '-')
      return longoption(argc, argv, optstring, options, position);
    next = argument + 1;
  }
  option = (unsigned char) *next++;
  spec = option == ':' ? NULL : strchr(optstring, option);
  if (spec && spec[1] == ':') {
    if (*next)
      optarg = next;
    else if (longmode && spec[2] == ':')
      optarg = NULL;
    else if (optind + 1 < argc && argv[optind + 1])
      optarg = argv[++optind];
    else {
      missing = 1;
      /* Skip the missing argument position as well as the option. */
      if (!longmode && optind < INT_MAX - 1)
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

int
getopt(int argc, char *const argv[], const char *optstring)
{
  int result = parse(argc, argv, optstring, 0, NULL, NULL);
  index = optind;
  return result;
}

int
getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts,
            int *longindex)
{
  int result = parse(argc, argv, optstring, 1, longopts, longindex);
  index = optind;
  return result;
}
