#ifndef _TEST_ENVUSER_H
#define _TEST_ENVUSER_H
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/boot.h>
#define ENVCHECK(c)                                                                                \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(180 + (int) count);                                                                    \
    count++;                                                                                       \
  } while (0)
static u32
envusercheck(const Bootview *boot)
{
  char      **original = environ, **before;
  char        duplicate[] = "DUP=one", keep[] = "KEEP=ok", other[] = "DUP=two";
  char        name[] = "OWN", value[] = "first", borrowed[] = "B=borrowed";
  char       *fixture[4], *external[3];
  const char *aliasname, *aliasvalue;
  u32         count = 0, i;
  errno = EDOM;
  if (bootresource(boot, BOOT_CREATE)) {
    before = environ;
    ENVCHECK(setenv("NATIVE_ENV", "value", 1) == -1 && errno == ENOMEM && environ == before &&
             !getenv("NATIVE_ENV"));
    return count;
  }
  fixture[0] = duplicate;
  fixture[1] = keep;
  fixture[2] = other;
  fixture[3] = NULL;
  environ = fixture;
  ENVCHECK(!unsetenv("DUP") && !getenv("DUP") && !strcmp(getenv("KEEP"), "ok") && errno == EDOM);
  before = environ;
  ENVCHECK(!unsetenv("absent") && environ == before && errno == EDOM);
  ENVCHECK(unsetenv("") == -1 && errno == EINVAL);
  ENVCHECK(setenv("bad=name", "v", 1) == -1 && errno == EINVAL);
  ENVCHECK(setenv("", "v", 1) == -1 && errno == EINVAL);
  errno = EDOM;
  ENVCHECK(!setenv(name, value, 1) && errno == EDOM);
  name[0] = 'X';
  value[0] = 'Z';
  ENVCHECK(!strcmp(getenv("OWN"), "first") && !strcmp(getenv("KEEP"), "ok") &&
           !strcmp(duplicate, "DUP=one") && !strcmp(other, "DUP=two"));
  before = environ;
  ENVCHECK(!setenv("OWN", "ignored", 0) && environ == before && !strcmp(getenv("OWN"), "first"));
  ENVCHECK(!setenv("OWN", "second", -1) && !strcmp(getenv("OWN"), "second") && errno == EDOM);
  ENVCHECK(!setenv("EMPTY", "", 1) && getenv("EMPTY") && !*getenv("EMPTY"));
  ENVCHECK(!setenv("NAME", "ALIAS", 1));
  aliasname = getenv("NAME");
  aliasvalue = getenv("OWN");
  ENVCHECK(!setenv(aliasname, aliasvalue, 1) && !strcmp(getenv("ALIAS"), "second"));
  external[0] = getenv("OWN") - 4;
  external[1] = borrowed;
  external[2] = NULL;
  environ = external;
  ENVCHECK(!setenv("NEW", "V", 1) && errno == EDOM);
  borrowed[2] = 'Z';
  ENVCHECK(!strcmp(getenv("OWN"), "second") && !strcmp(getenv("B"), "borrowed") &&
           !strcmp(getenv("NEW"), "V"));
  ENVCHECK(!unsetenv("OWN") && !getenv("OWN") && errno == EDOM);
  ENVCHECK(!unsetenv("NEW") && !getenv("NEW"));
  ENVCHECK(!unsetenv("B") && !environ[0] && errno == EDOM);
  for (i = 0; i < 64; i++)
    if (setenv("LOOP", "value", 1) || strcmp(getenv("LOOP"), "value") || errno != EDOM)
      _exit(239);
  ENVCHECK(!unsetenv("LOOP") && !environ[0]);
  environ = NULL;
  ENVCHECK(!unsetenv("absent") && !setenv("FRESH", "V", 1) && !strcmp(getenv("FRESH"), "V"));
  ENVCHECK(!unsetenv("FRESH") && !environ[0] && errno == EDOM);
  environ = original;
  return count;
}
#undef ENVCHECK
#endif
