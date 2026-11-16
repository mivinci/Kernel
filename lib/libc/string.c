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
