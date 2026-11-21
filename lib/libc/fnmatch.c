#include <ctype.h>
#include <fnmatch.h>
#include <string.h>

typedef struct Charclass Charclass;
struct Charclass
{
  const char *name;
  int (*contains)(int);
};

static const Charclass classes[] = {
    {"alnum", isalnum}, {"alpha", isalpha}, {"blank", isblank}, {"cntrl", iscntrl},
    {"digit", isdigit}, {"graph", isgraph}, {"lower", islower}, {"print", isprint},
    {"punct", ispunct}, {"space", isspace}, {"upper", isupper}, {"xdigit", isxdigit},
};

/* Return the next bracket item. Only single-byte collating elements exist
   in the C locale. A class or equivalence expression is not a range bound. */
static const char *
bracketitem(const char *p, int flags, int byte, int *bound, int *matched)
{
  const char *end;
  size_t      length, i;
  char        kind;
  *bound = -1;
  *matched = 0;
  if (*p == '[' && p[1] && strchr(":.=", p[1])) {
    kind = p[1];
    end = p + 2;
    while (*end && !(*end == kind && end[1] == ']'))
      end++;
    if (!*end)
      return NULL;
    length = (size_t) (end - (p + 2));
    if (kind == ':') {
      for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
        if (strlen(classes[i].name) == length && !memcmp(p + 2, classes[i].name, length)) {
          *matched = classes[i].contains(byte);
          return end + 2;
        }
      return NULL;
    }
    if (length != 1)
      return NULL;
    if (kind == '.')
      *bound = (unsigned char) p[2];
    *matched = byte == (unsigned char) p[2];
    return end + 2;
  }
  if (*p == '\\' && !(flags & FNM_NOESCAPE))
    p++;
  if (!*p)
    return NULL;
  *bound = (unsigned char) *p;
  *matched = byte == *bound;
  return p + 1;
}

/* A first ']' is a member. An unclosed '[' is matched as a literal. */
static const char *
bracket(const char *p, int flags, int byte, int *matched)
{
  int negate = *p == '!', found = 0;
  int first, last, hit, ignored;
  p += negate;
  do {
    p = bracketitem(p, flags, byte, &first, &hit);
    if (!p)
      return NULL;
    if (*p == '-' && p[1] && p[1] != ']' && first >= 0) {
      p = bracketitem(p + 1, flags, byte, &last, &ignored);
      if (!p || last < 0)
        return NULL;
      hit = first <= byte && byte <= last;
    }
    found |= hit;
  } while (*p != ']');
  *matched = !!found != negate;
  return p + 1;
}

int
fnmatch(const char *pattern, const char *string, int flags)
{
  const char *p = pattern, *s = string, *star = NULL, *retry = NULL, *next;
  int         pathname = flags & FNM_PATHNAME;
  int         byte, token, wildcard, matched, leading;
  for (;;) {
    byte = (unsigned char) *s;
    leading = (flags & FNM_PERIOD) && byte == '.' && (s == string || (pathname && s[-1] == '/'));
    if (*p == '*' && !leading) {
      do
        p++;
      while (*p == '*');
      star = p;
      retry = s;
      continue;
    }
    if (!*p && !byte)
      return 0;
    token = (unsigned char) *p;
    next = p + 1;
    wildcard = token == '?' || token == '[' || token == '*';
    matched = token == '?' && byte;
    if (token == '\\' && !(flags & FNM_NOESCAPE)) {
      if (!*next)
        return FNM_NOMATCH;
      token = (unsigned char) *next++;
    } else if (token == '[' && byte && !leading && !(pathname && byte == '/')) {
      next = bracket(p + 1, flags, byte, &matched);
      if (!next) {
        next = p + 1;
        wildcard = 0;
      }
    }
    if ((!wildcard && token == byte) ||
        (wildcard && matched && !leading && !(pathname && byte == '/'))) {
      p = next;
      s++;
      /* A star in the previous pathname component can no longer matter. */
      if (pathname && byte == '/')
        star = NULL;
      continue;
    }
    if (!star || !*retry || (pathname && *retry == '/'))
      return FNM_NOMATCH;
    p = star;
    s = ++retry;
  }
}
