#ifndef _LIBSYS_SYSCALL_H
#define _LIBSYS_SYSCALL_H
#include <types.h>
#include <abi/base.h>
#include <abi/mem.h>
#include <abi/proc.h>
#include <abi/signal.h>
typedef struct Sysret  Sysret;
typedef struct Forkret Forkret;
struct Sysret
{
  u64 values[7], image;
};
struct Forkret
{
  Handle      construct, pid;
  const void *fix;
};
STATIC_ASSERT(sizeof(Sysret) == 64, syscall_return_size);
STATIC_ASSERT(offsetof(Sysret, values) == 0, syscall_return_values);
STATIC_ASSERT(offsetof(Sysret, image) == 56, syscall_return_image);
STATIC_ASSERT(offsetof(Forkret, construct) == 0, fork_return_construct);
STATIC_ASSERT(offsetof(Forkret, pid) == 8, fork_return_pid);
STATIC_ASSERT(offsetof(Forkret, fix) == 16, fork_return_fix);
extern const u8 __sys_forkreturn_begin[], __sys_forkreturn_end[];
/* Decode only a successful child return within the dedicated window.
   Returns 1 on capture, 0 for unrelated frames, or a negative native error. */
isize sysforkdecode(const void *frame, u64 begin, u64 end, Forkret *out);
/* Architecture-only marshaling. The argument array has exactly seven words. */
isize         __sys_enter(u64 number, const u64 args[7], void *out);
NORETURN void __sys_exit(u64 code);
isize         __sys_yield(void);
isize         __sys_getpg(Handle pid, Handle *pgid, Handle *sid);
isize         __sys_setpg(Handle pid, Handle pgid);
isize         __sys_setsid(Handle *sid);
isize         __sys_getid(Handle *pid, u64 *image, Handle *parent);
isize         __sys_alloc(Handle authority, const Allocarg *args, Handle *out);
isize         __sys_copy(void *destination, const void *source, u64 bytes);
isize         __sys_free(Handle resource);
isize         __sys_debughandoff(Handle system);
isize         __sys_ttycreate(Handle system, Handle control, Handle *resource, Handle *id);
isize         __sys_ttybind(Handle terminal, Handle sid);
isize         __sys_getfg(Handle terminal, Handle *pgid, Handle *sid, u64 *epoch);
isize         __sys_clock_gettime(u64 clock, int64_t *seconds, u64 *nanoseconds);
isize         __sys_clock_getres(u64 clock, int64_t *seconds, u64 *nanoseconds);
isize         __sys_alarmset(u64 deadline, u64 *generation);
isize         __sys_alarmcancel(u64 *generation);
isize __sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 image, u64 *cookie,
                    u64 *state, u64 *reason);
isize __sys_obsread(u64 cookie, u64 *state, u64 *reason, Handle *pid, u64 *image);
isize __sys_obsack(u64 cookie);
isize __sys_obsdrop(u64 cookie);
isize __sys_inherit(Handle resource, u64 flags);
isize __sys_map(Handle space, const Maparg *args);
isize __sys_unmap(Handle space, u64 address);
isize __sys_seal(Handle memory);
isize __sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *records, u64 count,
                  u64 *written, u64 *next);
isize __sys_create(Handle authority, const Createarg *args, Handle *view, Handle *pid,
                   Handle *control);
isize __sys_configure(Handle view, const Config *config, u64 reserve);
isize __sys_connect(Handle authority, u64 kind, u64 rights, u64 bits, Handle *out);
isize __sys_grant(Handle target, Handle source, u64 rights, u64 offset, u64 bytes, u64 inherit,
                  Handle *out);
isize __sys_start(Handle view);
isize __sys_revoke(Handle target, Handle resource);
isize __sys_terminate(Handle control);
isize __sys_destroy(Handle view);
isize __sys_forkprep(Handle authority, u64 reserve, Forkret *out);
isize __sys_forkcommit(Handle construct, const void *fix, u64 bytes, Handle *pid);
isize __sys_forkabort(Handle construct);
isize __sys_execprep(Handle space, u64 reserve, Handle *view, u64 *image);
isize __sys_execabort(Handle view);
isize __sys_getcred(Handle authority, Handle pid, u64 image, u32 ids[4]);
isize __sys_sigaction(u64 signum, const Sigact *action, Sigact *old);
isize __sys_sigmask(u64 how, u64 mask, u64 *old);
isize __sys_sigpending(u64 *out);
isize __sys_sigsuspend(u64 mask);
isize __sys_sigsend(const Sigsel *selector, u64 signum);
isize __sys_irqack(Handle binding);
isize __sys_irqbind(Handle target, Handle source, u32 bit, Handle *binding);
isize __sys_irqunbind(Handle target, Handle binding);
NORETURN void __sys_sigreturn(void *frame);
isize __sys_wait(Handle selector, u64 options, Handle *pid, u64 *kind, u64 *code, u64 *sequence);
#endif
