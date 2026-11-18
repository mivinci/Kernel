#ifndef _INIT_MANIFEST_H
#define _INIT_MANIFEST_H
#include <types.h>
#include <abi/boot.h>
#define INITCFG_MAGIC      UINT64_C(0x4b53455256494331)
#define INITCFG_FORMAT     1
#define INITCFG_SERVICES   16
#define INITCFG_NAME_BYTES 64
#define INITCFG_SERIAL     1
#define INITCFG_REQUIRED   1
#define INITCFG_RECORD     160
#define INITCFG_MAX_PAGES  4096
#define INITCFG_MAX_EXEC   6144
#define INITCFG_PENDING    16

typedef struct Servicecfg Servicecfg;
typedef struct Manifest   Manifest;
struct Servicecfg
{
  u64 number, kind, flags, creator, memlimit, exec_limit, resources, pending, dependencies;
  u8  image[INITCFG_NAME_BYTES];
  u64 reserved[3];
};
struct Manifest
{
  u64        count;
  Servicecfg service[INITCFG_SERVICES];
};
STATIC_ASSERT(sizeof(Servicecfg) == INITCFG_RECORD, initcfg_record_size);
STATIC_ASSERT(offsetof(Servicecfg, image) == 72, initcfg_name_offset);
/* Private init format, decoded from immutable bytes. Failure leaves out intact. */
isize manifestopen(const void *data, u64 bytes, u64 machine, Manifest *out);
#endif
