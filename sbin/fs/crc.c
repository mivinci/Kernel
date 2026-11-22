#include "crc.h"

u32
fscrc32c(u32 state, const void *bytes, usize length)
{
  const u8 *input = bytes;
  u32       bit;
  while (length--) {
    state ^= *input++;
    for (bit = 0; bit < 8; bit++)
      state = (state >> 1) ^ (UINT32_C(0x82f63b78) & ((u32) 0 - (state & 1)));
  }
  return state;
}
