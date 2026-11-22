#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "miniz.h"

int
main(void)
{
  static const unsigned char input[] = "Kernel SDK: miniz round trip. "
                                       "Kernel SDK: miniz round trip. "
                                       "Kernel SDK: miniz round trip. "
                                       "Kernel SDK: miniz round trip. ";
  unsigned char              compressed[256], decoded[sizeof(input) - 1];
  mz_ulong                   packed = sizeof(compressed), unpacked = sizeof(decoded), crc;
  char                       line[80];
  int                        length;
  size_t                     sent = 0;
  if (mz_compress(compressed, &packed, input, sizeof(input) - 1) != MZ_OK ||
      mz_uncompress(decoded, &unpacked, compressed, packed) != MZ_OK ||
      unpacked != sizeof(decoded) || memcmp(input, decoded, sizeof(decoded)))
    return EXIT_FAILURE;
  crc = mz_crc32(0, decoded, sizeof(decoded));
  if (crc != 0x1f77b35cUL)
    return EXIT_FAILURE;
  length = snprintf(line, sizeof(line), "sdk-a2 miniz bytes=%lu crc32=%08lx\n", unpacked, crc);
  if (length < 0 || (size_t) length >= sizeof(line))
    return EXIT_FAILURE;
  while (sent < (size_t) length) {
    ssize_t count = write(1, line + sent, (size_t) length - sent);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return EXIT_FAILURE;
    sent += (size_t) count;
  }
  return EXIT_SUCCESS;
}
