#ifndef _BOOTIMAGE_H
#define _BOOTIMAGE_H
#include <abi/boot.h>
/* Immutable descriptor bytes, including unaligned input. Decode into owned
   values; do not retain typed pointers into the wire image. Failure zeros out.
   out must be disjoint from data. Ordinary ELF parsing belongs to userspace. */
int bootimagecheck(const void *data, u64 available, Bootimage *out);
/* Use only after bootimagecheck on the same immutable input, index < count. */
void bootimagesegment(const void *data, const Bootimage *image, u64 index, Bootseg *out);
/* Only a still-unpublished PID 1. Caller treats any post-admission failure as
   fatal; partial initialization is never published or retried as another init. */
int bootimageload(Handle pid, const void *data, u64 bytes, const void *startup, u64 startupbytes);
#endif
