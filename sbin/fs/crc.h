#ifndef _FS_CRC_H
#define _FS_CRC_H
#include <types.h>

/* Extend a raw CRC32C accumulator. The record codec supplies the initial
   state and applies the final XOR only after the last span. */
u32 fscrc32c(u32 state, const void *bytes, usize length);
#endif
