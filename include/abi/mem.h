#ifndef _ABI_MEM_H
#define _ABI_MEM_H
#include <types.h>
#include <abi/base.h>
#define ALLOC_DATA   1
#define ALLOC_SHARED 2
#define ALLOC_CODE   3
#define ALLOC_DMA    4
typedef struct Allocarg Allocarg;
struct Allocarg
{
  u64 bytes, kind, align, minaddr, maxaddr, reserved;
};
typedef struct Maparg Maparg;
struct Maparg
{
  Handle mem;
  u64    offset, address, length, prot, reserved;
};
STATIC_ASSERT(sizeof(Allocarg) == 48, allocarg_size);
STATIC_ASSERT(offsetof(Allocarg, reserved) == 40, allocarg_reserved);
STATIC_ASSERT(sizeof(Maparg) == 48, maparg_size);
STATIC_ASSERT(offsetof(Maparg, reserved) == 40, maparg_reserved);
#endif
