#include <string.h>
#include <types.h>
void *
memcpy(void *destination, const void *source, size_t bytes)
{
  u8       *to = destination;
  const u8 *from = source;
  size_t    i;
  for (i = 0; i < bytes; i++)
    to[i] = from[i];
  return destination;
}
void *
mempcpy(void *destination, const void *source, size_t bytes)
{
  if (!bytes)
    return destination;
  return (u8 *) memcpy(destination, source, bytes) + bytes;
}
void *
memmove(void *destination, const void *source, size_t bytes)
{
  u8       *to = destination;
  const u8 *from = source;
  size_t    i;
  if ((u64) to <= (u64) from || (u64) to - (u64) from >= bytes)
    return memcpy(destination, source, bytes);
  for (i = bytes; i; i--)
    to[i - 1] = from[i - 1];
  return destination;
}
void *
memset(void *destination, int byte, size_t bytes)
{
  u8    *to = destination;
  size_t i;
  for (i = 0; i < bytes; i++)
    to[i] = (u8) byte;
  return destination;
}
int
memcmp(const void *a, const void *b, size_t bytes)
{
  const u8 *first = a, *second = b;
  size_t    i;
  for (i = 0; i < bytes; i++)
    if (first[i] != second[i])
      return (int) first[i] - (int) second[i];
  return 0;
}
size_t
strlen(const char *string)
{
  size_t bytes = 0;
  while (string[bytes])
    bytes++;
  return bytes;
}
int
strcmp(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (int) (u8) *a - (int) (u8) *b;
}
int
strcoll(const char *a, const char *b)
{
  return strcmp(a, b);
}
size_t
strxfrm(char *destination, const char *source, size_t bytes)
{
  size_t length = strlen(source);
  /* The C locale uses the original bytes as the collation key. */
  if (length < bytes)
    memcpy(destination, source, length + 1);
  return length;
}
char *
strchr(const char *string, int byte)
{
  for (;;) {
    if ((u8) *string == (u8) byte)
      return (char *) string;
    if (!*string)
      return NULL;
    string++;
  }
}
char *
strchrnul(const char *string, int byte)
{
  while (*string && (u8) *string != (u8) byte)
    string++;
  return (char *) string;
}
void *
memchr(const void *memory, int byte, size_t bytes)
{
  const u8 *data = memory;
  size_t    i;
  for (i = 0; i < bytes; i++)
    if (data[i] == (u8) byte)
      return (void *) (data + i);
  return NULL;
}
size_t
strnlen(const char *string, size_t bytes)
{
  size_t length = 0;
  while (length < bytes && string[length])
    length++;
  return length;
}
int
strncmp(const char *a, const char *b, size_t bytes)
{
  size_t i;
  for (i = 0; i < bytes; i++) {
    if ((u8) a[i] != (u8) b[i])
      return (int) (u8) a[i] - (int) (u8) b[i];
    if (!a[i])
      return 0;
  }
  return 0;
}
char *
strrchr(const char *string, int byte)
{
  const char *last = NULL;
  for (;;) {
    if ((u8) *string == (u8) byte)
      last = string;
    if (!*string)
      return (char *) last;
    string++;
  }
}
char *
strstr(const char *string, const char *pattern)
{
  const char *at, *next;
  if (!*pattern)
    return (char *) string;
  for (; *string; string++) {
    at = string;
    next = pattern;
    while (*next && *at == *next) {
      at++;
      next++;
    }
    if (!*next)
      return (char *) string;
  }
  return NULL;
}
size_t
strspn(const char *string, const char *accepted)
{
  size_t length = 0;
  while (string[length] && strchr(accepted, (u8) string[length]))
    length++;
  return length;
}
size_t
strcspn(const char *string, const char *rejected)
{
  size_t length = 0;
  while (string[length] && !strchr(rejected, (u8) string[length]))
    length++;
  return length;
}
char *
strpbrk(const char *string, const char *accepted)
{
  for (; *string; string++)
    if (strchr(accepted, (u8) *string))
      return (char *) string;
  return NULL;
}
char *
strcpy(char *destination, const char *source)
{
  stpcpy(destination, source);
  return destination;
}
char *
stpcpy(char *destination, const char *source)
{
  size_t i;
  for (i = 0; source[i]; i++)
    destination[i] = source[i];
  destination[i] = 0;
  return destination + i;
}
char *
strncpy(char *destination, const char *source, size_t bytes)
{
  stpncpy(destination, source, bytes);
  return destination;
}
char *
stpncpy(char *destination, const char *source, size_t bytes)
{
  size_t i, end;
  for (i = 0; i < bytes && source[i]; i++)
    destination[i] = source[i];
  end = i;
  for (; i < bytes; i++)
    destination[i] = 0;
  return destination + end;
}
char *
strcat(char *destination, const char *source)
{
  strcpy(destination + strlen(destination), source);
  return destination;
}
char *
strncat(char *destination, const char *source, size_t bytes)
{
  size_t end = strlen(destination), i;
  for (i = 0; i < bytes && source[i]; i++)
    destination[end + i] = source[i];
  destination[end + i] = 0;
  return destination;
}
char *
strtok_r(char *string, const char *delimiters, char **state)
{
  char *end;
  if (!string)
    string = *state;
  if (!string)
    return NULL;
  string += strspn(string, delimiters);
  if (!*string) {
    *state = string;
    return NULL;
  }
  end = string + strcspn(string, delimiters);
  if (*end)
    *end++ = 0;
  *state = end;
  return string;
}
char *
strtok(char *string, const char *delimiters)
{
  static char *state;
  return strtok_r(string, delimiters, &state);
}
