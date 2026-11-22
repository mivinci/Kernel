#include <ctype.h>
#include <strings.h>

int
strncasecmp(const char *a, const char *b, size_t bytes)
{
  while (bytes) {
    int left = tolower((unsigned char) *a++);
    int right = tolower((unsigned char) *b++);
    if (left != right || !left)
      return left - right;
    bytes--;
  }
  return 0;
}

int
strcasecmp(const char *a, const char *b)
{
  return strncasecmp(a, b, (size_t) -1);
}
