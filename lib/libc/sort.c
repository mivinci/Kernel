#include <stdlib.h>

static void
swap(unsigned char *a, unsigned char *b, size_t width)
{
  while (width--) {
    unsigned char byte = *a;
    *a++ = *b;
    *b++ = byte;
  }
}

static void
sift(unsigned char *base, size_t root, size_t count, size_t width,
     int (*compare)(const void *, const void *))
{
  /* Only internal nodes have children, so doubling root cannot wrap. */
  while (root < count / 2) {
    size_t child = root * 2 + 1;
    if (child + 1 < count && compare(base + child * width, base + (child + 1) * width) < 0)
      child++;
    if (compare(base + root * width, base + child * width) >= 0)
      break;
    swap(base + root * width, base + child * width, width);
    root = child;
  }
}

void
qsort(void *base, size_t count, size_t width, int (*compare)(const void *, const void *))
{
  unsigned char *bytes = base;
  size_t         root = count / 2;
  if (!width)
    return;
  while (root) {
    root--;
    sift(bytes, root, count, width, compare);
  }
  while (count > 1) {
    count--;
    swap(bytes, bytes + count * width, width);
    sift(bytes, 0, count, width, compare);
  }
}

void *
bsearch(const void *key, const void *base, size_t count, size_t width,
        int (*compare)(const void *, const void *))
{
  const unsigned char *bytes = base;
  while (count) {
    size_t               half = count / 2;
    const unsigned char *item = bytes + half * width;
    int                  order = compare(key, item);
    if (!order)
      return (void *) item;
    if (order < 0)
      count = half;
    else {
      bytes = item + width;
      count -= half + 1;
    }
  }
  return 0;
}
