#ifndef _RQ_H
#define _RQ_H
#include <image.h>
#include <list.h>
#include <abi/ipc.h>
#include <abi/event.h>
#include <abi/proc.h>
#ifndef REQUESTS
#define REQUESTS 1024
#endif
#ifndef RECOVERY_RQS
#define RECOVERY_RQS 64
#endif
#ifndef PENDING_CALLS
#define PENDING_CALLS 16
#endif
STATIC_ASSERT(PENDING_CALLS > 0 && PENDING_CALLS <= 16, pending_capacity);
#define OUT_REQUESTS  12
#define RQ_NORMAL     0
#define RQ_SIGNAL1    1
#define RQ_SIGNAL2    2
#define RQ_CONTROL    3
#define RQUOTA_OPEN   1
#define RQUOTA_CLOSED 2
#define RMAIL_OPEN    1
#define RMAIL_CLOSING 2
#define RMAIL_DRAINED 3
struct Rtable;
typedef struct Rqid     Rqid;
typedef struct Rquota   Rquota;
typedef struct Rmail    Rmail;
typedef struct Rqrecv   Rqrecv;
typedef struct Rqresult Rqresult;
typedef struct Rqinfo   Rqinfo;
struct Rqid
{
  u64 generation;
  u32 index, reserved;
};
struct Rquota
{
  Lifeid owner;
  u16    slots[OUT_REQUESTS];
  u32    state;
};
struct Rmail
{
  Imageid image;
  List    queue, owned;
  u32     immediate, deferred, state, limit;
};
struct Rqrecv
{
  Msg    message;
  Handle source, request;
  u64    image;
  u32    kind, reserved;
};
struct Rqresult
{
  Msg   message;
  isize status;
};
struct Rqinfo
{
  u64    generation;
  Handle caller, server, authority;
  u64    callerimage, serverimage;
  isize  status;
  u32    state, flags, refs, ordinal;
};
STATIC_ASSERT(REQUESTS > RECOVERY_RQS && REQUESTS <= 1024 && RECOVERY_RQS >= 0 &&
                  PROCS * OUT_REQUESTS + RECOVERY_RQS <= REQUESTS,
              rq_capacity);
STATIC_ASSERT(sizeof(Rquota) == 48 && sizeof(Rmail) == 64, rq_proc_budget);
/* Trusted mechanisms, IRQs masked. These do not resolve native CALL authority,
   wake threads, validate CONTROL, or prove downstream/DMA safety. */
int rqreserve(Rquota *quota, Lifeid owner, u32 recovery);
int rqquotaend(Rquota *quota);
int rqmailinit(Rmail *mail, Imageid image);
/* Before publication only, preserves one immediate dispatch slot. */
int rqmailconfigure(Rmail *mail, u32 pending);
int rqmailclose(Rmail *mail, u32 batch);
/* After ending the old image and before activating its replacement, abandon
   the old caller's results and hand its inbox to bounded CPU cleanup. */
int rqexec(Rquota *quota, Rmail *mail, Rmail *retiring, Imageid next);
int rqsubmit(Rquota *quota, Rmail *server, Imageid caller, Handle authority, u32 context, u32 kind,
             const Msg *message, u32 options, Rqid *out);
int rqsubmitto(Rquota *quota, Rmail *server, Imageid caller, Handle authority, u32 context,
               u32 kind, const Msg *message, u32 options, Rqid *out, struct Rtable *receiving);
Handle rqhandle(Rqid id, u32 kind);
int    rqlookup(Handle rpc, Imageid caller, Rqid *out);
int    rqrecv(Rmail *mail, Rqrecv *out);
int    rqdefer(Rmail *mail, Handle request);
int    rqreply(Rmail *mail, Handle request, const Msg *reply);
int    rqreplyresource(Rmail *mail, struct Rtable *source, Handle request, const Resreply *reply);
int    rqresultkind(Rqid id);
int    rqfinish(Rmail *mail, Handle request);
int    rqcheck(Rmail *mail, Handle request);
int    rqsource(Rmail *mail, Handle request, Imageid *out);
int    rqresult(Rqid id, Imageid caller, u32 options, Rqresult *out);
int    rqcancel(Rqid id, Imageid caller);
int    rqack(Rqid id, Imageid caller, u32 options);
int    rqabandon(Rqid id, Imageid caller);
int    rqrevoke(Rquota *quota, Imageid caller, Handle authority);
int    rqclean(Rqid id, Imageid server);
int    rqhold(Rqid id);
int    rqdrop(Rqid id);
int    rqstat(Rqid id, Rqinfo *out);
/* Caller validates query authority. image=0 selects the bound Life history. */
int rqquery(Handle pid, u64 image, u64 cursor, Queryrec *records, u32 count, u32 *written,
            u64 *next);
/* Same visible records as rqquery, counted once even for a self request. */
int rqcount(Handle pid, u64 image, u32 *out);
/* Consume hint marks only; persistent results/cancellation remain unchanged. */
u64 rqsignals(Rquota *quota, Rmail *mail);
#endif
