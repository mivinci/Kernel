#ifndef _ABI_BOOT_H
#define _ABI_BOOT_H
#include <types.h>
#include <abi/base.h>
#include <abi/boot.inc>
#define BOOT_MAGIC          UINT64_C(0x4b424f4f54303031)
#define BOOT_MAX_BYTES      65536
#define BOOT_ARG_BYTES      16384
#define BOOT_ARG_COUNT      256
#define BOOT_RESOURCE_COUNT 128
#define BOOT_FD_COUNT       128
#define BOOT_SELF_SPACE     1
#define BOOT_SELF_ALLOC     2
#define BOOT_CREATE         3
#define BOOT_FS_CALL        4
#define BOOT_TTY_CALL       5
#define BOOT_TERMINAL       6
#define BOOT_SYSTEM         7
#define BOOT_IMAGE          8
#define BOOT_USER_LO        UINT64_C(0x10000)
#define BOOT_USER_HI        (UINT64_C(1) << 38)
/* Top 32 MiB: read-only package window, followed by Boot/stack startup area. */
#define BOOT_IMAGE_HI      UINT64_C(0x3ffe000000)
#define BOOT_PACKAGE_LO    BOOT_IMAGE_HI
#define BOOT_PACKAGE_HI    UINT64_C(0x3fff000000)
#define BOOT_LOAD_SEGMENTS 64
#define BOOT_LOAD_MAGIC    UINT64_C(0x4b494d4730303031)
typedef struct Bootimage Bootimage;
typedef struct Bootseg   Bootseg;
typedef struct Boot      Boot;
typedef struct Strref    Strref;
typedef struct Resentry  Resentry;
typedef struct Bootfd    Bootfd;
struct Boot
{
  u64 magic, format, bytes, flags, argc, argv_off, envc, env_off;
  u64 resource_count, resources_off, fd_count, fds_off, fix_off, fix_bytes, user_lo, user_hi;
  u64 package_va, package_bytes;
};
struct Bootimage
{
  u64 magic, format, bytes, entry, segment_count, segments_off, boot_off, boot_bytes;
};
struct Bootseg
{
  u64 file_off, file_bytes, va, mem_bytes, prot, reserved;
};
STATIC_ASSERT(sizeof(Bootimage) == 64, boot_image_header);
STATIC_ASSERT(sizeof(Bootseg) == 48, boot_segment_entry);
struct Strref
{
  u32 offset, length;
};
struct Resentry
{
  u64    tag;
  Handle handle;
  u64    flags, reserved;
};
struct Bootfd
{
  u64    fd, flags;
  Handle backend_call;
  u64    session, prepare_token, reserved;
};
STATIC_ASSERT(sizeof(Strref) == 8, boot_string_entry);
STATIC_ASSERT(sizeof(Resentry) == 32, boot_resource_entry);
STATIC_ASSERT(sizeof(Bootfd) == 48, boot_fd_entry);
STATIC_ASSERT(offsetof(Bootfd, prepare_token) == 32, boot_prepare_token);
STATIC_ASSERT(sizeof(Boot) == BOOT_HEADER_BYTES, boot_header);
STATIC_ASSERT(offsetof(Boot, bytes) == 16, boot_bytes);
STATIC_ASSERT(offsetof(Boot, user_lo) == 112, boot_user_lo);
STATIC_ASSERT(offsetof(Boot, user_hi) == 120, boot_user_hi);
STATIC_ASSERT(offsetof(Boot, package_va) == BOOT_PACKAGE_VA_OFFSET, boot_package_va);
STATIC_ASSERT(offsetof(Boot, package_bytes) == BOOT_PACKAGE_BYTES_OFFSET, boot_package_bytes);
#endif
