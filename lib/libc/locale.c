#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static const char *const categories[] = {"LC_CTYPE",   "LC_NUMERIC",  "LC_TIME",
                                         "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES"};
static char              cname[] = "C", point[] = ".", empty[] = "";

static int
clocale(const char *name)
{
  return !strcmp(name, "C") || !strcmp(name, "POSIX");
}

char *
setlocale(int category, const char *name)
{
  const char *value, *fallback;
  int         first, last, i;
  if (category < LC_CTYPE || category > LC_ALL)
    return NULL;
  if (!name)
    return cname;
  if (*name)
    return clocale(name) ? cname : NULL;
  value = getenv("LC_ALL");
  if (value && *value)
    return clocale(value) ? cname : NULL;
  fallback = getenv("LANG");
  first = category == LC_ALL ? LC_CTYPE : category;
  last = category == LC_ALL ? LC_MESSAGES : category;
  for (i = first; i <= last; i++) {
    value = getenv(categories[i]);
    if (!value || !*value)
      value = fallback;
    if (value && *value && !clocale(value))
      return NULL;
  }
  return cname;
}

struct lconv *
localeconv(void)
{
  static struct lconv values = {point,    empty,    empty,    empty,    empty,    empty,
                                empty,    empty,    empty,    empty,    CHAR_MAX, CHAR_MAX,
                                CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
                                CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX};
  return &values;
}
