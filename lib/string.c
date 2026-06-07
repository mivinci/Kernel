#include <libc.h>

size_t strlen(const char *p) {
  size_t n = 0;
  while (*p++)
    n++;
  return n;
}

void *memset(void *s, int c, size_t n) {
  char *p = s;
  while (n--)
    *p++ = (char)c;
  return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
  char       *d = dst;
  const char *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  char       *d = dst;
  const char *s = src;
  if (d <= s || d >= s + n) {
    /* Non-overlapping or dst before src: copy forward */
    while (n--)
      *d++ = *s++;
  } else {
    /* Overlapping with dst after src: copy backward */
    d += n - 1;
    s += n - 1;
    while (n--)
      *d-- = *s--;
  }
  return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *a = s1, *b = s2;
  while (n--) {
    if (*a != *b) return *a - *b;
    a++;
    b++;
  }
  return 0;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dst, const char *src) {
  char *ret = dst;
  while ((*dst++ = *src++) != '\0')
    ;
  return ret;
}
