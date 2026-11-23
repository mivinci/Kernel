#include <rq.h>
#include <local.h>
#define RF_DONE_PENDING   1024
#define RF_CANCEL_PENDING 2048
#define RF_DEFERRED       16
#define RF_SEND           32
#define RF_ASYNC          64
#define RF_CLOSED         128
#define RF_RECOVERY       256
#define RF_ORPHAN         512
#define RF_RECV_MEM       4096
#define RF_RECV_CALL      8192
#define RF_RESOURCE_REPLY 16384
typedef struct Rq Rq;
struct Rq
{
  u64     generation;
  Lifeid  owner;
  Imageid callerid, serverid;
  Handle  caller, server;
  u64     callerimage, serverimage;
  Handle  authority;
  Msg     message;
  List    queue, owned;
  isize   status;
  u32     state, flags, refs;
  u16     ordinal, resource;
  Rtable *receiving;
};
STATIC_ASSERT(sizeof(Rq) <= 192, rq_record_budget);
static Rq   requests[REQUESTS];
static List retired;
static u32  initialized;
static int
same(Imageid a, Imageid b)
{
  return a.generation == b.generation && a.index == b.index && !a.reserved && !b.reserved;
}
static int
owneris(const Rq *r, Lifeid life)
{
  return r->owner.generation == life.generation && r->owner.index == life.index && !life.reserved &&
         life.generation;
}
static Rq *
find(Rqid id)
{
  Rq *r;
  if (id.reserved || id.index >= REQUESTS)
    return NULL;
  r = &requests[id.index];
  return r->state && r->generation == id.generation ? r : NULL;
}
static Rqid
identity(Rq *r)
{
  Rqid id;
  id.generation = r->generation;
  id.index = (u32) (r - requests);
  id.reserved = 0;
  return id;
}
static void
copymsg(Msg *to, const Msg *from)
{
  u32 i;
  for (i = 0; i < 4; i++)
    to->words[i] = from ? from->words[i] : 0;
}
static void
unreserve(Rq *r)
{
  if (r->state)
    panic("active request unreserved");
  if (lifedrop(r->owner, LIFE_CLEANUP))
    panic("request owner reference");
  r->owner.generation = 0;
}
static void
reap(Rq *r)
{
  u32 closed;
  if (r->state != RQ_TERMINAL || (r->flags & 7) != 7 || r->refs)
    return;
  if (!listempty(&r->queue) || !listempty(&r->owned))
    panic("linked request reclaimed");
  if (r->receiving)
    panic("terminal receive slot retained");
  closed = r->flags & RF_CLOSED;
  if (imagedrop(r->callerid, IMAGE_CLEANUP) || imagedrop(r->serverid, IMAGE_CLEANUP))
    panic("request image reference");
  r->state = 0;
  r->flags &= RF_RECOVERY;
  if (closed)
    unreserve(r);
}
static void
terminal(Rq *r, isize status, const Msg *message)
{
  if (r->state == RQ_TERMINAL)
    return;
  if (r->state == RQ_QUEUED) {
    listdel(&r->queue);
    r->flags |= RQ_FINISHED;
  }
  if (r->state == RQ_ACCEPTED && status < 0)
    r->flags |= RF_CANCEL_PENDING;
  if (r->receiving) {
    if (localreturn(r->receiving, r->callerid,
                    handlepack(HANDLE_RPC, r->generation, (u32) (r - requests)), r->resource))
      panic("request receive-slot ownership");
    r->receiving = NULL;
    r->resource = 0xffffU;
  }
  r->flags |= RF_DONE_PENDING;
  r->state = RQ_TERMINAL;
  r->status = status;
  copymsg(&r->message, message);
}
int
rqreserve(Rquota *quota, Lifeid owner, u32 recovery)
{
  Life life;
  u16  slots[OUT_REQUESTS];
  u32  i, count = 0, begin, end;
  if (!quota || quota->state == RQUOTA_OPEN || recovery > 1)
    return -ERR_INVALID;
  if (lifestat(owner, &life))
    return -ERR_STALE;
  if (life.state != LIFE_CONSTRUCT && life.state != LIFE_LIVE)
    return -ERR_STATE;
  if (life.cleanup > 0xffffffffU - OUT_REQUESTS)
    return -ERR_OVERFLOW;
  if (!initialized) {
    listinit(&retired);
    initialized = 1;
  }
  begin = recovery ? REQUESTS - RECOVERY_RQS : 0;
  end = recovery ? REQUESTS : REQUESTS - RECOVERY_RQS;
  for (i = 0; i < REQUESTS; i++) {
    Rq *r = &requests[i];
    if (owneris(r, owner))
      return -ERR_STATE;
    if (i >= begin && i < end && !r->owner.generation && r->generation < HANDLE_GENERATION_MAX &&
        count < OUT_REQUESTS)
      slots[count++] = (u16) i;
  }
  if (count != OUT_REQUESTS)
    return -ERR_CAPACITY;
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq *r = &requests[slots[i]];
    if (lifehold(owner, LIFE_CLEANUP))
      panic("request admission reservation");
    r->owner.generation = owner.generation;
    r->owner.index = owner.index;
    r->owner.reserved = 0;
    r->flags = recovery ? RF_RECOVERY : 0;
    r->ordinal = (u16) i;
    r->state = r->refs = 0;
    r->receiving = NULL;
    r->resource = 0xffffU;
    listinit(&r->queue);
    listinit(&r->owned);
    quota->slots[i] = slots[i];
  }
  quota->owner.generation = owner.generation;
  quota->owner.index = owner.index;
  quota->owner.reserved = 0;
  quota->state = RQUOTA_OPEN;
  return 0;
}
int
rqmailinit(Rmail *mail, Imageid image)
{
  Image info;
  if (!mail || (mail->state && mail->state != RMAIL_DRAINED))
    return -ERR_STATE;
  if (imagestat(image, &info) || info.state == IMAGE_ENDED)
    return -ERR_STALE;
  mail->image.generation = image.generation;
  mail->image.index = image.index;
  mail->image.reserved = 0;
  listinit(&mail->queue);
  listinit(&mail->owned);
  mail->immediate = PG_NONE;
  mail->deferred = 0;
  mail->limit = PENDING_CALLS;
  mail->state = RMAIL_OPEN;
  return 0;
}
static int
mailactive(Rmail *mail)
{
  Image image;
  return mail && mail->state == RMAIL_OPEN && !imagestat(mail->image, &image) &&
         image.state == IMAGE_ACTIVE;
}
int
rqmailconfigure(Rmail *mail, u32 pending)
{
  Image image;
  if (!mail || !pending || pending > PENDING_CALLS)
    return -ERR_INVALID;
  if (mail->state != RMAIL_OPEN || imagestat(mail->image, &image) ||
      image.state != IMAGE_PREPARING || !listempty(&mail->queue) || !listempty(&mail->owned) ||
      mail->immediate != PG_NONE || mail->deferred)
    return -ERR_STATE;
  mail->limit = pending;
  return 0;
}

int
rqsubmitto(Rquota *quota, Rmail *server, Imageid caller, Handle authority, u32 context, u32 kind,
           const Msg *message, u32 options, Rqid *out, Rtable *receiving)
{
  Image source, target;
  Rq   *r = NULL;
  u32   i, first, end, exhausted = 0;
  int   error;
  if (!quota || quota->state != RQUOTA_OPEN || !message || !out || context > RQ_CONTROL ||
      (kind != IPC_CALL && kind != IPC_SEND) || handletype(authority) != HANDLE_RESOURCE ||
      options & ~(CALL_ASYNC | CALL_RECV_MEM | CALL_RECV_CALL | CALL_CONTROL) ||
      (kind == IPC_SEND && options) ||
      (options & (CALL_RECV_MEM | CALL_RECV_CALL)) == (CALL_RECV_MEM | CALL_RECV_CALL))
    return -ERR_INVALID;
  if ((options & (CALL_RECV_MEM | CALL_RECV_CALL)) && !receiving)
    return -ERR_NOTSUP;
  if (imagestat(caller, &source) || source.state != IMAGE_ACTIVE ||
      source.life.generation != quota->owner.generation || source.life.index != quota->owner.index)
    return -ERR_STATE;
  if (!server || server->state != RMAIL_OPEN || imagestat(server->image, &target))
    return -ERR_PEER;
  if (target.state != IMAGE_ACTIVE)
    return target.state == IMAGE_PREPARING ? -ERR_STATE : -ERR_PEER;
  if (options & CALL_CONTROL)
    context = RQ_CONTROL;
  first = context == RQ_NORMAL ? 0 : context == RQ_SIGNAL1 ? 8 : context == RQ_SIGNAL2 ? 9 : 10;
  end = context == RQ_NORMAL ? 8 : context == RQ_CONTROL ? 12 : first + 1;
  for (i = first; i < end; i++) {
    if (quota->slots[i] >= REQUESTS)
      panic("request quota index");
    r = &requests[quota->slots[i]];
    if (!owneris(r, quota->owner) || r->ordinal != i || r->flags & RF_CLOSED)
      panic("request quota ownership");
    if (!r->state) {
      if (r->generation < HANDLE_GENERATION_MAX)
        break;
      exhausted = 1;
    }
  }
  if (i == end)
    return exhausted ? -ERR_OVERFLOW : -ERR_CAPACITY;
  error = imagehold(caller, IMAGE_CLEANUP);
  if (error)
    return error;
  error = imagehold(server->image, IMAGE_CLEANUP);
  if (error) {
    imagedrop(caller, IMAGE_CLEANUP);
    return error;
  }
  r->receiving = NULL;
  r->resource = 0xffffU;
  if (options & (CALL_RECV_MEM | CALL_RECV_CALL)) {
    error =
        localreceive(receiving, caller,
                     handlepack(HANDLE_RPC, r->generation + 1, (u32) (r - requests)), &r->resource);
    if (error) {
      imagedrop(caller, IMAGE_CLEANUP);
      imagedrop(server->image, IMAGE_CLEANUP);
      return error;
    }
    r->receiving = receiving;
  }
  r->generation++;
  r->callerid.generation = caller.generation;
  r->callerid.index = caller.index;
  r->callerid.reserved = 0;
  r->serverid.generation = server->image.generation;
  r->serverid.index = server->image.index;
  r->serverid.reserved = 0;
  r->caller = source.pid;
  r->callerimage = source.number;
  r->server = target.pid;
  r->serverimage = target.number;
  r->authority = authority;
  copymsg(&r->message, message);
  r->state = RQ_QUEUED;
  r->flags = (r->flags & RF_RECOVERY) | (kind == IPC_SEND ? RF_SEND : 0) |
             (options & CALL_ASYNC ? RF_ASYNC : 0) | (options & CALL_RECV_MEM ? RF_RECV_MEM : 0) |
             (options & CALL_RECV_CALL ? RF_RECV_CALL : 0);
  r->status = 0;
  r->refs = 0;
  listtail(&server->queue, &r->queue);
  *out = identity(r);
  return 0;
}
int
rqsubmit(Rquota *quota, Rmail *server, Imageid caller, Handle authority, u32 context, u32 kind,
         const Msg *message, u32 options, Rqid *out)
{
  return rqsubmitto(quota, server, caller, authority, context, kind, message, options, out, NULL);
}
Handle
rqhandle(Rqid id, u32 kind)
{
  Rq *r = find(id);
  if (!r || r->flags & RF_SEND || (kind != HANDLE_RPC && kind != HANDLE_REQUEST))
    return 0;
  return handlepack(kind, id.generation, id.index);
}
int
rqlookup(Handle rpc, Imageid caller, Rqid *out)
{
  Rqid id;
  Rq  *r;
  if (!out || handletype(rpc) != HANDLE_RPC)
    return -ERR_BAD_HANDLE;
  id.generation = handlegen(rpc);
  id.index = handleindex(rpc);
  id.reserved = 0;
  r = find(id);
  if (!r)
    return -ERR_STALE;
  if (!same(r->callerid, caller) || r->flags & RF_SEND)
    return -ERR_BAD_HANDLE;
  if (r->flags & RQ_RELEASED)
    return -ERR_STALE;
  *out = id;
  return 0;
}
int
rqrecv(Rmail *mail, Rqrecv *out)
{
  Rq   *r;
  Image source;
  u32   i;
  if (!out || !mailactive(mail))
    return -ERR_STATE;
  if (mail->immediate != PG_NONE)
    return -ERR_BUSY;
  if (listempty(&mail->queue))
    return -ERR_WOULD_BLOCK;
  r = (Rq *) ((u8 *) mail->queue.next - offsetof(Rq, queue));
  if (imagestat(r->callerid, &source))
    panic("queued source image reference");
  if (source.state == IMAGE_ENDED) {
    if (rqabandon(identity(r), r->callerid))
      panic("queued source abandonment");
    return 1;
  }
  copymsg(&out->message, &r->message);
  out->source = r->caller;
  out->image = r->callerimage;
  out->request = 0;
  out->kind = r->flags & RF_SEND ? IPC_SEND : IPC_CALL;
  out->reserved = 0;
  if (r->flags & RF_SEND) {
    terminal(r, 0, NULL);
    reap(r);
  } else {
    i = (u32) (r - requests);
    listdel(&r->queue);
    listtail(&mail->owned, &r->owned);
    r->state = RQ_ACCEPTED;
    mail->immediate = i;
    out->request = rqhandle(identity(r), HANDLE_REQUEST);
  }
  return 0;
}
static Rq *
received(Rmail *mail, Handle handle)
{
  Rqid id;
  Rq  *r;
  if (!mailactive(mail) || handletype(handle) != HANDLE_REQUEST)
    return NULL;
  id.generation = handlegen(handle);
  id.index = handleindex(handle);
  id.reserved = 0;
  r = find(id);
  return r && !(r->flags & (RF_SEND | RQ_FINISHED | RF_ORPHAN)) && same(r->serverid, mail->image) &&
                 !listempty(&r->owned)
             ? r
             : NULL;
}
int
rqdefer(Rmail *mail, Handle request)
{
  Rq *r = received(mail, request);
  if (!r)
    return -ERR_BAD_HANDLE;
  if (r->state != RQ_ACCEPTED || r->flags & RF_DEFERRED)
    return -ERR_STATE;
  if (mail->deferred >= mail->limit - 1)
    return -ERR_BUSY;
  if (mail->immediate != (u32) (r - requests))
    panic("request immediate ownership");
  mail->immediate = PG_NONE;
  mail->deferred++;
  r->flags |= RF_DEFERRED;
  return 0;
}
static void
finishowned(Rmail *mail, Rq *r)
{
  if (r->flags & RF_DEFERRED) {
    if (!mail->deferred)
      panic("request deferred count");
    mail->deferred--;
  } else {
    if (mail->immediate != (u32) (r - requests))
      panic("request immediate count");
    mail->immediate = PG_NONE;
  }
  listdel(&r->owned);
  r->flags |= RQ_FINISHED | RF_DONE_PENDING;
  reap(r);
}
int
rqreply(Rmail *mail, Handle request, const Msg *reply)
{
  Rq *r = received(mail, request);
  if (!r)
    return -ERR_BAD_HANDLE;
  if (!reply)
    return -ERR_INVALID;
  if (r->state == RQ_ACCEPTED)
    terminal(r, 0, reply);
  else if (r->state != RQ_TERMINAL)
    return -ERR_STATE;
  finishowned(mail, r);
  return 0;
}
int
rqreplyresource(Rmail *mail, Rtable *source, Handle request, const Resreply *reply)
{
  Rq    *r = received(mail, request);
  Image  service;
  Msg    message;
  Handle handle;
  u32    kind, i;
  int    error;
  if (!r)
    return -ERR_BAD_HANDLE;
  if (!source || source->state != LOCAL_OPEN || imagestat(mail->image, &service) ||
      service.state != IMAGE_ACTIVE || source->owner != service.pid ||
      source->image != service.number)
    return -ERR_BAD_HANDLE;
  kind = r->flags & RF_RECV_MEM ? LOCAL_MEM : r->flags & RF_RECV_CALL ? LOCAL_CALL : 0;
  error = localreplycheck(source, kind, reply);
  if (error)
    return error;
  if (r->state == RQ_TERMINAL) {
    /* Forced termination already returned the receive slot. A valid late
       descriptor can finish service responsibility, never install into a new table. */
    finishowned(mail, r);
    return 0;
  }
  if (r->state != RQ_ACCEPTED || !r->receiving)
    return -ERR_STATE;
  error = localdeliver(source, kind, reply, r->receiving, r->callerid,
                       handlepack(HANDLE_RPC, r->generation, (u32) (r - requests)), r->resource,
                       &handle);
  if (error)
    return error;
  r->receiving = NULL;
  r->resource = 0xffffU;
  message.words[0] = (u64) handle;
  for (i = 0; i < 3; i++)
    message.words[i + 1] = reply->business[i];
  r->flags |= RF_RESOURCE_REPLY;
  terminal(r, 0, &message);
  finishowned(mail, r);
  return 0;
}
int
rqresultkind(Rqid id)
{
  Rq *r = find(id);
  if (!r)
    return -ERR_STALE;
  return r->state == RQ_TERMINAL && !r->status && (r->flags & RF_RESOURCE_REPLY)
             ? IPC_RESOURCE_REPLY
             : IPC_NONE;
}
int
rqfinish(Rmail *mail, Handle request)
{
  Rq *r = received(mail, request);
  if (!r)
    return -ERR_BAD_HANDLE;
  if (r->state != RQ_TERMINAL)
    return -ERR_STATE;
  finishowned(mail, r);
  return 0;
}
int
rqsource(Rmail *mail, Handle request, Imageid *out)
{
  Rq *r = received(mail, request);
  if (!out)
    return -ERR_INVALID;
  out->generation = 0;
  out->index = out->reserved = 0;
  if (!r)
    return -ERR_BAD_HANDLE;
  out->generation = r->callerid.generation;
  out->index = r->callerid.index;
  return 0;
}
int
rqcheck(Rmail *mail, Handle request)
{
  Rq *r = received(mail, request);
  if (!r)
    return -ERR_BAD_HANDLE;
  return r->state == RQ_TERMINAL ? 4 : r->flags & RQ_CANCEL_REQUESTED ? 3 : 1;
}
int
rqresult(Rqid id, Imageid caller, u32 options, Rqresult *out)
{
  Rq *r = find(id);
  if (!r || r->flags & RQ_RELEASED)
    return -ERR_STALE;
  if (!same(r->callerid, caller))
    return -ERR_BAD_HANDLE;
  if (options & ~RQ_FINISH || !out)
    return -ERR_INVALID;
  if (options == RQ_FINISH) {
    if (!(r->flags & RQ_FINISHED))
      return -ERR_WOULD_BLOCK;
    copymsg(&out->message, NULL);
    out->status = 0;
    return 0;
  }
  if (r->flags & RQ_CONSUMED)
    return -ERR_STATE;
  if (r->state != RQ_TERMINAL)
    return -ERR_WOULD_BLOCK;
  copymsg(&out->message, &r->message);
  out->status = r->status;
  return 0;
}
int
rqcancel(Rqid id, Imageid caller)
{
  Rq *r = find(id);
  if (!r || r->flags & RQ_RELEASED)
    return -ERR_STALE;
  if (!same(r->callerid, caller))
    return -ERR_BAD_HANDLE;
  if (r->state == RQ_QUEUED)
    terminal(r, -ERR_CANCELLED, NULL);
  else if (r->state == RQ_ACCEPTED && !(r->flags & RQ_CANCEL_REQUESTED))
    r->flags |= RQ_CANCEL_REQUESTED | RF_CANCEL_PENDING;
  return 0;
}
int
rqack(Rqid id, Imageid caller, u32 options)
{
  Rq *r = find(id);
  if (!r || r->flags & RQ_RELEASED)
    return -ERR_STALE;
  if (!same(r->callerid, caller))
    return -ERR_BAD_HANDLE;
  if (!options || options & ~(ACK_RESULT | ACK_RELEASE))
    return -ERR_INVALID;
  if (r->state != RQ_TERMINAL ||
      (options & ACK_RELEASE &&
       (!(r->flags & RQ_FINISHED) || (!(r->flags & RQ_CONSUMED) && !(options & ACK_RESULT)))))
    return -ERR_STATE;
  if (options & ACK_RESULT)
    r->flags |= RQ_CONSUMED;
  if (options & ACK_RELEASE)
    r->flags |= RQ_RELEASED;
  reap(r);
  return 0;
}
int
rqabandon(Rqid id, Imageid caller)
{
  Rq   *r = find(id);
  Image image;
  if (!r)
    return -ERR_STALE;
  if (!same(r->callerid, caller))
    return -ERR_BAD_HANDLE;
  if (imagestat(caller, &image) || image.state != IMAGE_ENDED)
    return -ERR_STATE;
  terminal(r, -ERR_PEER, NULL);
  r->flags |= RQ_CONSUMED | RQ_RELEASED;
  reap(r);
  return 0;
}
int
rqquotaend(Rquota *quota)
{
  u32 i;
  if (!quota || quota->state != RQUOTA_OPEN)
    return -ERR_STATE;
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq   *r;
    Image image;
    if (quota->slots[i] >= REQUESTS)
      panic("request closing quota index");
    r = &requests[quota->slots[i]];
    if (!owneris(r, quota->owner))
      panic("request closing quota owner");
    if (r->state && (imagestat(r->callerid, &image) || image.state != IMAGE_ENDED))
      return -ERR_STATE;
  }
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq *r = &requests[quota->slots[i]];
    if (!r->state)
      unreserve(r);
    else {
      r->flags |= RF_CLOSED;
      if (rqabandon(identity(r), r->callerid))
        panic("request owner abandonment");
    }
  }
  quota->state = RQUOTA_CLOSED;
  return 0;
}
int
rqmailclose(Rmail *mail, u32 batch)
{
  u32   i;
  Image image;
  if (!mail || !batch || batch > 32 || !mail->state)
    return -ERR_INVALID;
  if (mail->state == RMAIL_DRAINED)
    return 0;
  if (imagestat(mail->image, &image))
    return -ERR_STALE;
  if (image.state == IMAGE_ACTIVE)
    return -ERR_STATE;
  mail->state = RMAIL_CLOSING;
  for (i = 0; i < batch; i++) {
    Rq *r;
    if (!listempty(&mail->queue)) {
      r = (Rq *) ((u8 *) mail->queue.next - offsetof(Rq, queue));
      terminal(r, -ERR_PEER, NULL);
      reap(r);
    } else if (!listempty(&mail->owned)) {
      r = (Rq *) ((u8 *) mail->owned.next - offsetof(Rq, owned));
      terminal(r, -ERR_PEER, NULL);
      if (r->flags & RF_DEFERRED) {
        if (!mail->deferred)
          panic("retired deferred count");
        mail->deferred--;
      } else {
        if (mail->immediate != (u32) (r - requests))
          panic("retired immediate count");
        mail->immediate = PG_NONE;
      }
      listmove(&retired, &r->owned);
      r->flags |= RF_ORPHAN;
    } else {
      if (mail->immediate != PG_NONE || mail->deferred)
        panic("mailbox cleanup counts");
      mail->state = RMAIL_DRAINED;
      return 0;
    }
  }
  return 1;
}
int
rqrevoke(Rquota *quota, Imageid caller, Handle authority)
{
  u32 i, count = 0;
  if (!quota || quota->state != RQUOTA_OPEN)
    return -ERR_STATE;
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq *r = &requests[quota->slots[i]];
    if (r->state == RQ_QUEUED && same(r->callerid, caller) && r->authority == authority) {
      terminal(r, -ERR_REVOKED, NULL);
      count++;
    }
  }
  return (int) count;
}
int
rqclean(Rqid id, Imageid server)
{
  Rq   *r = find(id);
  Image image;
  if (!r)
    return -ERR_STALE;
  if (!same(r->serverid, server))
    return -ERR_BAD_HANDLE;
  if (!(r->flags & RF_ORPHAN) || r->state != RQ_TERMINAL || r->flags & RQ_FINISHED ||
      imagestat(server, &image) || image.state != IMAGE_ENDED)
    return -ERR_STATE;
  listdel(&r->owned);
  r->flags |= RQ_FINISHED | RF_DONE_PENDING;
  reap(r);
  return 0;
}
int
rqhold(Rqid id)
{
  Rq *r = find(id);
  if (!r)
    return -ERR_STALE;
  if (r->refs == 0xffffffffU)
    return -ERR_OVERFLOW;
  r->refs++;
  return 0;
}
int
rqdrop(Rqid id)
{
  Rq *r = find(id);
  if (!r || !r->refs)
    return -ERR_STALE;
  r->refs--;
  reap(r);
  return 0;
}
int
rqstat(Rqid id, Rqinfo *out)
{
  Rq *r = find(id);
  if (!r || !out)
    return -ERR_STALE;
  out->generation = r->generation;
  out->caller = r->caller;
  out->server = r->server;
  out->authority = r->authority;
  out->callerimage = r->callerimage;
  out->serverimage = r->serverimage;
  out->status = r->status;
  out->state = r->state;
  out->flags = r->flags;
  out->refs = r->refs;
  out->ordinal = r->ordinal;
  return 0;
}

static void
takechain(List *to, List *from)
{
  listinit(to);
  if (!listempty(from)) {
    to->next = from->next;
    to->prev = from->prev;
    to->next->prev = to;
    to->prev->next = to;
    listinit(from);
  }
}

int
rqexec(Rquota *quota, Rmail *mail, Rmail *retiring, Imageid next)
{
  Image old, target;
  u32   i;
  if (!quota || quota->state != RQUOTA_OPEN || !mail || mail->state != RMAIL_OPEN || !retiring ||
      retiring == mail || (retiring->state && retiring->state != RMAIL_DRAINED))
    return -ERR_STATE;
  if (imagestat(mail->image, &old) || old.state != IMAGE_ENDED || old.reason != IMAGE_EXEC ||
      imagestat(next, &target) || target.state != IMAGE_PREPARING ||
      old.life.generation != quota->owner.generation || old.life.index != quota->owner.index ||
      target.life.generation != old.life.generation || target.life.index != old.life.index ||
      target.sourceimage != old.number)
    return -ERR_STATE;
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq *r = &requests[quota->slots[i]];
    if (r->state && same(r->callerid, mail->image) && rqabandon(identity(r), mail->image))
      panic("image request abandonment");
  }
  retiring->image = mail->image;
  retiring->immediate = mail->immediate;
  retiring->deferred = mail->deferred;
  retiring->limit = mail->limit;
  retiring->state = RMAIL_CLOSING;
  takechain(&retiring->queue, &mail->queue);
  takechain(&retiring->owned, &mail->owned);
  mail->image = next;
  mail->immediate = PG_NONE;
  mail->deferred = 0;
  return 0;
}

u64
rqsignals(Rquota *quota, Rmail *mail)
{
  u64   bits = 0;
  u32   i = 0;
  List *node;
  if (!quota || !mail || quota->state != RQUOTA_OPEN || mail->state != RMAIL_OPEN)
    return 0;
  for (i = 0; i < OUT_REQUESTS; i++) {
    Rq *r = &requests[quota->slots[i]];
    if (r->state && same(r->callerid, mail->image) && r->flags & RF_DONE_PENDING) {
      r->flags &= ~RF_DONE_PENDING;
      bits |= EVENT_RQ_DONE;
    }
  }
  i = 0;
  listforeach(node, &mail->owned)
  {
    Rq *r = (Rq *) ((u8 *) node - offsetof(Rq, owned));
    if (++i > PENDING_CALLS || !same(r->serverid, mail->image))
      panic("request signal ownership");
    if (r->flags & RF_CANCEL_PENDING) {
      r->flags &= ~RF_CANCEL_PENDING;
      bits |= EVENT_RQ_CANCEL;
    }
  }
  return bits;
}

static u32
queryroles(const Rq *r, Handle pid, u64 image)
{
  u32 roles = 0;
  if (!r->state)
    return 0;
  if (r->caller == pid && (!image || r->callerimage == image) &&
      (!image || !(r->flags & RQ_RELEASED)))
    roles |= 1;
  if (r->server == pid && (!image || r->serverimage == image) && !(r->flags & RQ_FINISHED) &&
      (!image || !(r->flags & RF_ORPHAN)))
    roles |= 2;
  return roles;
}
int
rqcount(Handle pid, u64 image, u32 *out)
{
  u32 i;
  if (!out)
    return -ERR_INVALID;
  *out = 0;
  if (!pidvalid(pid))
    return -ERR_INVALID;
  for (i = 0; i < REQUESTS; i++)
    if (queryroles(&requests[i], pid, image))
      (*out)++;
  return 0;
}
static void
queryrecord(const Rq *r, u32 index, Handle pid, u32 roles, Queryrec *record)
{
  u32 i;
  for (i = 0; i < sizeof(*record); i++)
    ((u8 *) record)[i] = 0;
  record->type = QUERY_RQ;
  record->pid = pid;
  record->image = roles & 1 ? r->callerimage : r->serverimage;
  record->stamp = (r->generation << 16) | index;
  record->data[0] = (u64) r->caller;
  record->data[1] = r->callerimage;
  record->data[2] = (u64) r->server;
  record->data[3] = r->serverimage;
  record->data[4] = r->state;
  record->data[5] = (u64) r->status;
  record->data[6] = r->flags & (RQ_CONSUMED | RQ_FINISHED | RQ_RELEASED | RQ_CANCEL_REQUESTED);
}
int
rqquery(Handle pid, u64 image, u64 cursor, Queryrec *records, u32 count, u32 *written, u64 *next)
{
  u32 index = 0, n = 0, roles;
  Rq *r;
  if (!written || !next)
    return -ERR_INVALID;
  *written = 0;
  *next = 0;
  if (!pidvalid(pid) || !records || !count || count > QUERY_RECORDS ||
      cursor >> 59)
    return -ERR_INVALID;
  if (cursor) {
    index = (u32) (cursor & 65535);
    if (!(cursor >> 16) || index >= REQUESTS)
      return -ERR_INVALID;
    r = &requests[index];
    if (r->generation != cursor >> 16 || !queryroles(r, pid, image))
      return -ERR_STALE;
  }
  for (; index < REQUESTS; index++) {
    r = &requests[index];
    roles = queryroles(r, pid, image);
    if (!roles)
      continue;
    if (n == count) {
      *next = (r->generation << 16) | index;
      break;
    }
    queryrecord(r, index, pid, roles, &records[n++]);
  }
  *written = n;
  return 0;
}
