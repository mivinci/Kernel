#ifndef _LIBSYS_SYSCALL_H
#define _LIBSYS_SYSCALL_H
#include <types.h>
#include <abi/base.h>
#include <abi/mem.h>
#include <abi/proc.h>
typedef struct Sysret Sysret;
struct Sysret
{
  u64 values[7], image;
};
STATIC_ASSERT(sizeof(Sysret) == 64, syscall_return_size);
STATIC_ASSERT(offsetof(Sysret, values) == 0, syscall_return_values);
STATIC_ASSERT(offsetof(Sysret, image) == 56, syscall_return_image);
/* Architecture-only marshaling. The argument array has exactly seven words. */
isize         __sys_enter(u64 number, const u64 args[7], void *out);
NORETURN void __sys_exit(u64 code);
isize         __sys_yield(void);
isize         __sys_getid(Handle *pid, u64 *image, Handle *parent);
isize         __sys_alloc(Handle authority, const Allocarg *args, Handle *out);
isize         __sys_copy(void *destination, const void *source, u64 bytes);
isize         __sys_free(Handle resource);
isize         __sys_map(Handle space, const Maparg *args);
isize         __sys_unmap(Handle space, u64 address);
isize         __sys_seal(Handle memory);
isize         __sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *records, u64 count,
                          u64 *written, u64 *next);
isize         __sys_create(Handle authority, const Createarg *args, Handle *view, Handle *pid,
                           Handle *control);
isize         __sys_configure(Handle view, const Config *config, u64 reserve);
isize         __sys_connect(Handle authority, u64 kind, u64 rights, u64 bits, Handle *out);
isize __sys_grant(Handle target, Handle source, u64 rights, u64 offset, u64 bytes, u64 inherit,
                  Handle *out);
isize __sys_start(Handle view);
isize __sys_revoke(Handle target, Handle resource);
isize __sys_terminate(Handle control);
isize __sys_destroy(Handle view);
isize __sys_getcred(Handle authority, Handle pid, u64 image, u32 ids[4]);
isize __sys_wait(Handle selector, u64 options, Handle *pid, u64 *kind, u64 *code, u64 *sequence);
#endif
