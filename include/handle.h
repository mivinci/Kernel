#ifndef _HANDLE_H
#define _HANDLE_H
#include <types.h>
#include <abi/base.h>
STATIC_ASSERT(sizeof(Handle) == 8, handle_size);
STATIC_ASSERT((Handle) -1 < 0, handle_signed);
/* Syntax only, never proof of object lifetime or authority. PID 1 is special. */
u32    handletype(Handle handle);
Handle handlepack(u32 kind, u64 generation, u32 index);
u64    handlegen(Handle handle);
u32    handleindex(Handle handle);
#endif
