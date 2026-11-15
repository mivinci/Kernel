#ifndef _ABI_BOOT_H
#define _ABI_BOOT_H
#include <types.h>
#define BOOT_MAGIC UINT64_C(0x4b424f4f54303031)
typedef struct Boot Boot;
struct Boot
{
  u64 magic, format, bytes, flags, argc, argv_off, envc, env_off;
  u64 resource_count, resources_off, fd_count, fds_off, fix_off, fix_bytes, user_lo, user_hi;
};
STATIC_ASSERT(sizeof(Boot) == 128, boot_header);
STATIC_ASSERT(offsetof(Boot, bytes) == 16, boot_bytes);
STATIC_ASSERT(offsetof(Boot, user_lo) == 112, boot_user_lo);
STATIC_ASSERT(offsetof(Boot, user_hi) == 120, boot_user_hi);
#endif
