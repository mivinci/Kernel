#include "ktest.h"
#include <proc.h>
#include <rq.h>
#include <diag.h>
#define ORDINARY_QUOTAS ((REQUESTS - RECOVERY_RQS) / OUT_REQUESTS)
typedef struct Actor Actor;
struct Actor
{
  Lifeid  life;
  Imageid image;
  Rquota  quota;
  Rmail   mail;
  Handle  pid;
};
static Rqid    draining[ORDINARY_QUOTAS - 1][OUT_REQUESTS];
static Imageid servers[ORDINARY_QUOTAS - 1];
static Lifeid  owners[ORDINARY_QUOTAS - 1];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("RQFAIL line=");                                                                      \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("request lifetime");                                                                   \
    }                                                                                              \
  } while (0)
static void
actor(Actor *a, Setid set, Handle pid)
{
  a->pid = pid;
  REQUIRE(!lifecreaterun(set, pid, 1, handleindex(pid), &a->life));
  REQUIRE(!imagecreate(a->life, 0, &a->image));
  REQUIRE(!rqreserve(&a->quota, a->life, 0));
  REQUIRE(!rqmailinit(&a->mail, a->image));
  REQUIRE(!lifepublish(a->life) && !imageactivate(a->image));
}
static void
endactor(Actor *a)
{
  Liferesult result;
  REQUIRE(!imageend(a->image, IMAGE_EXIT) && !lifeend(a->life, LIFE_EXIT, 42, NULL));
  REQUIRE(!rqquotaend(&a->quota));
  while (rqmailclose(&a->mail, 32) > 0) {
  }
  REQUIRE(!imagedetach(a->image) && !lifedetach(a->life));
  REQUIRE(!lifecollect(a->life, 1, &result) && result.code == 42);
}
static Rqid
submit(Actor *a, Actor *b, u32 kind, u64 value)
{
  Rqid id;
  Msg  message;
  u32  i;
  for (i = 0; i < 4; i++)
    message.words[i] = value + i;
  REQUIRE(!rqsubmit(&a->quota, &b->mail, a->image, handlepack(HANDLE_RESOURCE, 1, 7), RQ_NORMAL,
                    kind, &message, kind == IPC_SEND ? 0 : CALL_ASYNC, &id));
  return id;
}
static void
execmailtest(Setid set)
{
  static Actor  runner, peer;
  static Rmail  retiring;
  static Rtable resources;
  static Local  slots[4];
  Acctid        acct;
  Spaceid       space;
  Rqid          outgoing, ownqueued, incoming, queued, fresh, self, lookup;
  Rqrecv        received;
  Rqinfo        info;
  Rqresult      result;
  Imageid       old, next;
  Handle        authority = handlepack(HANDLE_RESOURCE, 1, 7), request, oldrpc;
  Msg           message = {{41, 0, 0, 0}};
  actor(&runner, set, handlepack(HANDLE_PROCESS, 707, 1));
  actor(&peer, set, handlepack(HANDLE_PROCESS, 707, 2));
  old = runner.image;
  REQUIRE(!acctcreate(set, 32, 32, &acct) && !spacecreate(acct, &space) &&
          !localinit(&resources, slots, 4, acct, space));
  resources.owner = runner.pid;
  resources.image = 1;
  REQUIRE(!rqsubmitto(&runner.quota, &peer.mail, old, authority, RQ_SIGNAL1, IPC_CALL, &message,
                      CALL_ASYNC | CALL_RECV_MEM, &outgoing, &resources) &&
          slots[0].subtype == LOCAL_RECEIVE && !rqrecv(&peer.mail, &received) &&
          !rqdefer(&peer.mail, received.request));
  request = received.request;
  oldrpc = rqhandle(outgoing, HANDLE_RPC);
  ownqueued = submit(&runner, &peer, IPC_CALL, 42);
  incoming = submit(&peer, &runner, IPC_CALL, 43);
  REQUIRE(!rqrecv(&runner.mail, &received) &&
          received.request == rqhandle(incoming, HANDLE_REQUEST));
  self = submit(&runner, &runner, IPC_CALL, 44);
  queued = submit(&peer, &runner, IPC_CALL, 45);
  REQUIRE(!imagecreate(runner.life, 1, &next) &&
          rqexec(&runner.quota, &runner.mail, &retiring, next) == -ERR_STATE && !retiring.state &&
          slots[0].subtype == LOCAL_RECEIVE && !imageend(old, IMAGE_EXEC) &&
          !rqexec(&runner.quota, &runner.mail, &retiring, next) && !imageactivate(next));
  runner.image = next;
  REQUIRE(!slots[0].subtype && rqlookup(oldrpc, next, &lookup) == -ERR_BAD_HANDLE &&
          rqstat(ownqueued, &info) == -ERR_STALE && rqstat(self, &info) == -ERR_STALE &&
          !rqstat(outgoing, &info) && info.status == -ERR_PEER && !(info.flags & RQ_FINISHED) &&
          rqcheck(&peer.mail, request) == 4 &&
          rqreply(&runner.mail, received.request, &message) == -ERR_BAD_HANDLE &&
          rqrecv(&runner.mail, &received) == -ERR_WOULD_BLOCK &&
          !rqsignals(&runner.quota, &runner.mail));
  REQUIRE(rqsubmit(&runner.quota, &peer.mail, next, authority, RQ_SIGNAL1, IPC_CALL, &message,
                   CALL_ASYNC, &fresh) == -ERR_CAPACITY);
  fresh = submit(&peer, &runner, IPC_CALL, 46);
  REQUIRE(!rqrecv(&runner.mail, &received) && received.message.words[0] == 46 &&
          !rqreply(&runner.mail, received.request, &message) && !rqack(fresh, peer.image, 3));
  REQUIRE(rqmailclose(&retiring, 1) == 1 && !rqresult(queued, peer.image, 0, &result) &&
          result.status == -ERR_PEER && !rqack(queued, peer.image, 3) &&
          rqresult(incoming, peer.image, 0, &result) == -ERR_WOULD_BLOCK &&
          rqmailclose(&retiring, 1) == 1 && !rqmailclose(&retiring, 1) &&
          !rqresult(incoming, peer.image, 0, &result) && result.status == -ERR_PEER &&
          rqclean(incoming, next) == -ERR_BAD_HANDLE && !rqclean(incoming, old) &&
          !rqack(incoming, peer.image, 3));
  REQUIRE(!rqreply(&peer.mail, request, &message) && !rqsignals(&runner.quota, &runner.mail) &&
          !rqsubmit(&runner.quota, &peer.mail, next, authority, RQ_SIGNAL1, IPC_CALL, &message,
                    CALL_ASYNC, &fresh) &&
          fresh.index == outgoing.index && fresh.generation > outgoing.generation &&
          !rqrecv(&peer.mail, &received) && received.image == 2 && received.source == runner.pid &&
          !rqreply(&peer.mail, received.request, &message) && !rqack(fresh, next, 3));
  REQUIRE(!imagedetach(old) && !localclose(&resources) && !localdrain(&resources, 4) &&
          !spacedestroy(space) && !acctclose(acct) && !acctunbind(acct));
  endactor(&runner);
  endactor(&peer);
}
void
rqtest(void)
{
  Setid    set;
  Set      group;
  Life     life;
  Lifeid   candidate, oldcaller, oldserver;
  Imageid  oldimage;
  Rquota   recovery;
  Handle   constructed;
  Pgstat   before, after;
  Actor    a, b;
  Rqid     id, pinned, fresh, stale;
  Rqrecv   received;
  Rqresult result;
  Rqinfo   info;
  Msg      reply;
  List     head, nodes[3], *node, *saved;
  u32      count;
  u32      i, j, step;
  Image    snapshot;
  Msg      bulk;
  int      error;
  u8      *byte;
  listinit(&head);
  for (i = 0; i < 3; i++)
    listinit(&nodes[i]);
  listtail(&head, &nodes[0]);
  listhead(&head, &nodes[1]);
  listtail(&head, &nodes[2]);
  REQUIRE(head.next == &nodes[1]);
  listmove(&head, &nodes[1]);
  REQUIRE(head.next == &nodes[0] && head.prev == &nodes[1]);
  count = 0;
  listforeach(node, &head)
  {
    count++;
  }
  REQUIRE(count == 3);
  listforeachsafe(node, saved, &head)
  {
    listdel(node);
  }
  REQUIRE(listempty(&head));
  for (i = 0; i < 3; i++)
    REQUIRE(listempty(&nodes[i]) && nodes[i].prev == &nodes[i]);
  REQUIRE(!setcreate(4096, 8192, 4, &set));
  pgstat(&before);
  execmailtest(set);
  byte = (u8 *) &a;
  for (i = 0; i < sizeof(a); i++)
    byte[i] = 0;
  byte = (u8 *) &b;
  for (i = 0; i < sizeof(b); i++)
    byte[i] = 0;
  actor(&b, set, handlepack(HANDLE_PROCESS, 1, PROCS - 2));
  for (j = 0; j < 4; j++)
    bulk.words[j] = j;
  for (i = 0; i + 1 < ORDINARY_QUOTAS; i++) {
    actor(&a, set, handlepack(HANDLE_PROCESS, i + 1, PROCS - 1));
    owners[i] = a.life;
    servers[i] = b.image;
    for (j = 0; j < OUT_REQUESTS; j++) {
      u32 context = j < 8 ? RQ_NORMAL : j == 8 ? RQ_SIGNAL1 : j == 9 ? RQ_SIGNAL2 : RQ_CONTROL;
      REQUIRE(!rqsubmit(&a.quota, &b.mail, a.image, handlepack(HANDLE_RESOURCE, 1, 7),
                        context == RQ_CONTROL ? RQ_NORMAL : context, IPC_CALL, &bulk,
                        CALL_ASYNC | (context == RQ_CONTROL ? CALL_CONTROL : 0), &draining[i][j]));
      REQUIRE(!rqrecv(&b.mail, &received) && !rqdefer(&b.mail, received.request));
    }
    endactor(&a);
    /* Rotate only the internal receiver Image; old owned nodes must retire. */
    REQUIRE(!imagestat(b.image, &snapshot) && !imageend(b.image, IMAGE_EXEC));
    REQUIRE(!rqmailclose(&b.mail, 32) && !imagedetach(b.image));
    REQUIRE(!imagecreate(b.life, snapshot.number, &b.image));
    REQUIRE(!rqmailinit(&b.mail, b.image) && !imageactivate(b.image));
    for (j = 0; j < OUT_REQUESTS; j++) {
      REQUIRE(!rqstat(draining[i][j], &info) && info.state == RQ_TERMINAL &&
              !(info.flags & RQ_FINISHED) && (info.flags & (RQ_CONSUMED | RQ_RELEASED)) == 5);
    }
  }
  REQUIRE(
      !lifecreaterun(set, handlepack(HANDLE_PROCESS, ORDINARY_QUOTAS + 1, PROCS - 1), 1, PROCS - 1, &candidate));
  byte = (u8 *) &recovery;
  for (i = 0; i < sizeof(recovery); i++)
    byte[i] = 0;
  REQUIRE(rqreserve(&recovery, candidate, 0) == -ERR_CAPACITY && !recovery.state);
  REQUIRE(!rqreserve(&recovery, candidate, 1));
  for (i = 0; i < OUT_REQUESTS; i++)
    REQUIRE(recovery.slots[i] >= REQUESTS - RECOVERY_RQS);
  REQUIRE(procnew(set, 1, 128, 256, LOCALS, &constructed) == -ERR_CAPACITY && !constructed);
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free);
  REQUIRE(!setstat(set, &group) && group.instances == 2 && !group.charged);
  for (j = 0; j < OUT_REQUESTS; j++)
    REQUIRE(!rqclean(draining[0][j], servers[0]));
  REQUIRE(!procnew(set, 1, 128, 256, LOCALS, &constructed) && !procdestroy(constructed));
  for (step = 0; step < 32; step++) {
    error = procclean(constructed);
    REQUIRE(error >= 0);
    if (!error)
      break;
  }
  REQUIRE(step < 32);
  for (i = 1; i + 1 < ORDINARY_QUOTAS; i++)
    for (j = 0; j < OUT_REQUESTS; j++)
      REQUIRE(!rqclean(draining[i][j], servers[i]));
  for (i = 0; i + 1 < ORDINARY_QUOTAS; i++)
    REQUIRE(lifestat(owners[i], &life) == -ERR_STALE);
  REQUIRE(!rqquotaend(&recovery) && !lifecancel(candidate) && !lifedetach(candidate));
  oldserver = b.life;
  endactor(&b);
  REQUIRE(lifestat(oldserver, &life) == -ERR_STALE);
  byte = (u8 *) &a;
  for (i = 0; i < sizeof(a); i++)
    byte[i] = 0;
  byte = (u8 *) &b;
  for (i = 0; i < sizeof(b); i++)
    byte[i] = 0;
  actor(&a, set, handlepack(HANDLE_PROCESS, 100, 1));
  actor(&b, set, handlepack(HANDLE_PROCESS, 100, 2));
  id = submit(&a, &b, IPC_CALL, 11);
  REQUIRE(!rqrecv(&b.mail, &received) && received.kind == IPC_CALL && received.source == a.pid &&
          received.image == 1 && received.request == rqhandle(id, HANDLE_REQUEST));
  REQUIRE(rqrecv(&b.mail, &received) == -ERR_BUSY);
  REQUIRE(!rqdefer(&b.mail, received.request) && !rqcancel(id, a.image));
  REQUIRE(rqcheck(&b.mail, received.request) == 3 &&
          rqfinish(&b.mail, received.request) == -ERR_STATE);
  REQUIRE(rqack(id, a.image, ACK_RESULT | ACK_RELEASE) == -ERR_STATE);
  for (i = 0; i < 4; i++)
    reply.words[i] = 128 + i;
  REQUIRE(!rqreply(&b.mail, received.request, &reply));
  REQUIRE(!rqresult(id, a.image, 0, &result) && !result.status && result.message.words[0] == 128);
  REQUIRE(!rqack(id, a.image, ACK_RESULT) && rqresult(id, a.image, 0, &result) == -ERR_STATE);
  REQUIRE(!rqresult(id, a.image, RQ_FINISH, &result) && !rqack(id, a.image, ACK_RELEASE));
  pinned = submit(&a, &b, IPC_CALL, 12);
  REQUIRE(!rqhold(pinned) && !rqcancel(pinned, a.image) && !rqack(pinned, a.image, 3));
  REQUIRE(!rqstat(pinned, &info) && info.refs == 1 && (info.flags & 7) == 7);
  id = submit(&a, &b, IPC_CALL, 13);
  REQUIRE(id.index != pinned.index && !rqdrop(pinned));
  fresh = submit(&a, &b, IPC_CALL, 14);
  REQUIRE(fresh.index == pinned.index && fresh.generation != pinned.generation &&
          rqstat(pinned, &info) == -ERR_STALE);
  REQUIRE(!rqcancel(id, a.image) && !rqack(id, a.image, 3));
  REQUIRE(!rqcancel(fresh, a.image) && !rqack(fresh, a.image, 3));
  id = submit(&a, &b, IPC_CALL, 21);
  REQUIRE(!rqhold(id) && !rqrecv(&b.mail, &received) && !rqdefer(&b.mail, received.request));
  oldcaller = a.life;
  endactor(&a);
  actor(&a, set, handlepack(HANDLE_PROCESS, 101, 1));
  fresh = submit(&a, &b, IPC_CALL, 22);
  REQUIRE(rqcheck(&b.mail, received.request) == 4 && !rqreply(&b.mail, received.request, &reply));
  REQUIRE(!rqstat(id, &info) && info.status == -ERR_PEER && !rqdrop(id));
  REQUIRE(lifestat(oldcaller, &life) == -ERR_STALE &&
          rqresult(fresh, a.image, 0, &result) == -ERR_WOULD_BLOCK);
  REQUIRE(!rqrecv(&b.mail, &received) && received.source == a.pid &&
          received.message.words[0] == 22);
  oldserver = b.life;
  oldimage = b.image;
  endactor(&b);
  REQUIRE(!rqresult(fresh, a.image, 0, &result) && result.status == -ERR_PEER);
  REQUIRE(!rqack(fresh, a.image, ACK_RESULT) && rqack(fresh, a.image, ACK_RELEASE) == -ERR_STATE);
  actor(&b, set, handlepack(HANDLE_PROCESS, 101, 2));
  REQUIRE(rqreply(&b.mail, received.request, &reply) == -ERR_BAD_HANDLE);
  REQUIRE(rqclean(fresh, b.image) == -ERR_BAD_HANDLE && !rqclean(fresh, oldimage));
  REQUIRE(!rqresult(fresh, a.image, RQ_FINISH, &result) && !rqack(fresh, a.image, ACK_RELEASE));
  REQUIRE(lifestat(oldserver, &life) == -ERR_STALE);
  stale = submit(&a, &b, IPC_SEND, 33);
  REQUIRE(!rqhandle(stale, HANDLE_RPC) && !rqrecv(&b.mail, &received) &&
          received.kind == IPC_SEND && !received.request && received.message.words[0] == 33);
  REQUIRE(!rqresult(stale, a.image, 0, &result) && !result.status && !rqack(stale, a.image, 3));
  oldcaller = a.life;
  oldserver = b.life;
  endactor(&a);
  endactor(&b);
  REQUIRE(lifestat(oldcaller, &life) == -ERR_STALE && lifestat(oldserver, &life) == -ERR_STALE);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("RQCHECK ordinary=");
  puthex(ORDINARY_QUOTAS);
  putstr(" draining=");
  puthex((ORDINARY_QUOTAS - 1) * OUT_REQUESTS);
  putstr(" recovery=");
  puthex(RECOVERY_RQS);
  putstr(" reused=");
  puthex(2);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
