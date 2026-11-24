#include <stddef.h>

/* Clang lowers some FS aggregate assignments to these calls. */
void *
memcpy(void *dst, const void *src, size_t bytes)
{
  unsigned char       *out = dst;
  const unsigned char *in = src;

  while (bytes--)
    *out++ = *in++;
  return dst;
}

void *
memset(void *dst, int value, size_t bytes)
{
  unsigned char *out = dst;

  while (bytes--)
    *out++ = (unsigned char) value;
  return dst;
}
