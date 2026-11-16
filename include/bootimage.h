#ifndef _BOOTIMAGE_H
#define _BOOTIMAGE_H
#include <abi/boot.h>
/* Trusted boot package descriptor; ordinary ELF parsing belongs to userspace. */
int bootimagecheck(const void *data, u64 available, const Bootimage **out);
/* Only a still-unpublished PID 1. Caller treats any post-admission failure as
   fatal; partial initialization is never published or retried as another init. */
int bootimageload(Handle pid, const void *data, u64 bytes, const void *startup, u64 startupbytes);
#endif
