/* Instance identity and result responsibility, independent of the run slot. */
#ifndef _LIFE_H
#define _LIFE_H
#include <acct.h>
#include <handle.h>
#include <group.h>
#ifndef LIFES
#define LIFES 256
#endif
#ifndef PROCS
#define PROCS 64
#endif
#define LIFE_CONSTRUCT 1
#define LIFE_LIVE      2
#define LIFE_ENDED     3
#define LIFE_ABORTED   4
#define LIFE_CONTROL   1
#define LIFE_OBSERVER  2
#define LIFE_CLEANUP   3
#define LIFE_EXIT      1
#define LIFE_SIGNAL    2
#define LIFE_FAULT     3

typedef struct Cred       Cred;
typedef struct Lifeid     Lifeid;
typedef struct Life       Life;
typedef struct Lifefault  Lifefault;
typedef struct Liferesult Liferesult;
struct Lifeid
{
  u64 generation;
  u32 index, reserved;
};
struct Lifefault
{
  u64 pc, address, cause;
};
struct Liferesult
{
  Handle pid;
  u64    sequence;
  u32    reason, code;
};
struct Cred
{
  u32 ruid, euid, rgid, egid;
};
struct Life
{
  u64       generation;
  Handle    pid, parent;
  u64       sequence, imagegen, account;
  u64       set;
  Handle    group;
  Lifefault fault;
  Cred      cred;
  u32       run, controls, observers, cleanup;
  u8        state, reason, code, waitref, instance;
};
STATIC_ASSERT(LIFES > 0 && LIFES <= 256 && PROCS > 0 && PROCS <= 64, life_capacity);
STATIC_ASSERT(sizeof(Cred) == 16, cred_size);
STATIC_ASSERT(sizeof(Life) <= 128, life_budget);
int lifecreate(Setid set, Handle pid, Handle parent, Lifeid *out);
/* Trusted boot/test constructor only; native CREATE must supply validated IDs. */
int lifecredentials(Lifeid id, const Cred *cred);
/* Construction binds one stable accounting identity without extending its life. */
int lifebindacct(Lifeid id, Acctid acct);
int lifeacct(Lifeid id, Acctid *out);
/* Trusted relationship mechanisms, not native setpg/setsid authorization. */
int lifebindgroup(Lifeid id, Handle group);
int lifesetgroup(Lifeid id, Handle group);
int lifegroup(Lifeid id, Handle *out);
int lifebootgroup(Lifeid id);
/* Existing published/result identity or a completed constructor, not authority. */
int lifefindpid(Handle pid, Lifeid *out);
int lifepublish(Lifeid id);
/* Allocate monotonically across discarded preparations, independent of Proc. */
int lifeimage(Lifeid id, u64 *number);
int lifecancel(Lifeid id);
int lifeend(Lifeid id, u32 reason, u32 code, const Lifefault *fault);
/* Detach only after the Proc owner has stopped and relinquished its resources. */
int lifedetach(Lifeid id);
int lifehold(Lifeid id, u32 kind);
int lifedrop(Lifeid id, u32 kind);
int lifereparent(Lifeid id, Handle oldparent, Handle newparent);
/* Trusted nonblocking result selection, not the native wait syscall. */
int lifecollect(Lifeid id, Handle parent, Liferesult *out);
int lifestat(Lifeid id, Life *out);
#endif
