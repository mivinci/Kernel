#include <libgen.h>
#include <string.h>

static char *
pathend(char *path)
{
  char *end = path + strlen(path);
  while (end != path && end[-1] == '/')
    end--;
  return end;
}

char *
basename(char *path)
{
  char *end, *start;
  if (!path || !*path)
    return ".";
  end = pathend(path);
  if (end == path)
    return "/";
  for (start = end; start != path && start[-1] != '/'; start--)
    ;
  if (*end)
    *end = 0;
  return start;
}

char *
dirname(char *path)
{
  char *end;
  if (!path || !*path)
    return ".";
  end = pathend(path);
  if (end == path)
    return "/";
  while (end != path && end[-1] != '/')
    end--;
  if (end == path)
    return ".";
  while (end != path && end[-1] == '/')
    end--;
  if (end == path)
    return "/";
  *end = 0;
  return path;
}
