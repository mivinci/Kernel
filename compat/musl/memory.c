#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

extern long sysvmget(unsigned long, void **);
extern long sysvmgetguarded(unsigned long, void **);
extern long sysvmput(void *);

void *
native_meta(size_t bytes)
{
  void *result;
  if (!sysvmgetguarded(bytes, &result))
    return result;
  errno = ENOMEM;
  return NULL;
}

void *
__mmap(void *address, size_t bytes, int prot, int flags, int fd, off_t offset)
{
  void *result;
  if (address || !bytes || bytes > SIZE_MAX - 4095 || prot != (PROT_READ | PROT_WRITE) ||
      flags != (MAP_PRIVATE | MAP_ANON) || fd != -1 || offset) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (!sysvmget((bytes + 4095) & ~(size_t) 4095, &result))
    return result;
  errno = ENOMEM;
  return MAP_FAILED;
}

void *mmap(void *, size_t, int, int, int, off_t) __attribute__((alias("__mmap")));

/* The static allocator only releases whole mappings by their base. */
int
munmap(void *address, size_t bytes)
{
  if (bytes && !sysvmput(address))
    return 0;
  errno = EINVAL;
  return -1;
}
