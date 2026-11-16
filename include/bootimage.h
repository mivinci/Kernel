#ifndef _BOOTIMAGE_H
#define _BOOTIMAGE_H
#include <abi/boot.h>
/* Trusted boot package descriptor; ordinary ELF parsing belongs to userspace. */
int bootimagecheck(const void *data, u64 available, const Bootimage **out);
#endif
