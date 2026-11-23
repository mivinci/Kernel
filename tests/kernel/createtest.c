#include "ktest.h"
#include <proc.h>
#include <rq.h>
#include <diag.h>
typedef struct Actor Actor;
struct Actor
{
  Lifeid  life;
  Imageid image;
  Rquota  quota;
  Rmail   mail;
};
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("CREATEFAIL line=");                                                                  \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("creation authority");                                                                 \
    }                                                                                              \
  } while (0)
static void
clean(Handle pid)
{
  int error;
  REQUIRE(!procdestroy(pid));
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error);
}
static void
actor(Actor *a, Setid set, u32 slot, u32 pending)
{
  u32 i;
  for (i = 0; i < sizeof(*a); i++)
    ((u8 *) a)[i] = 0;
  REQUIRE(!lifecreaterun(set, (2 + (1) * 64 + (slot)), 1, slot, &a->life));
  REQUIRE(!imagecreate(a->life, 0, &a->image) && !rqreserve(&a->quota, a->life, 0));
  REQUIRE(!rqmailinit(&a->mail, a->image) && !rqmailconfigure(&a->mail, pending));
  REQUIRE(!lifepublish(a->life) && !imageactivate(a->image));
}
static Rqid
submit(Actor *caller, Actor *server, u64 tag)
{
  Rqid id;
  Msg  message;
  message.words[0] = tag;
  message.words[1] = message.words[2] = message.words[3] = 0;
  REQUIRE(!rqsubmit(&caller->quota, &server->mail, caller->image, handlepack(HANDLE_RESOURCE, 1, 7),
                    RQ_NORMAL, IPC_CALL, &message, CALL_ASYNC, &id));
  return id;
}
static void
endactor(Actor *a)
{
  Liferesult result;
  REQUIRE(!imageend(a->image, IMAGE_EXIT) && !lifeend(a->life, LIFE_EXIT, 42, NULL));
  REQUIRE(!rqquotaend(&a->quota) && !rqmailclose(&a->mail, 32));
  REQUIRE(!imagedetach(a->image) && !lifedetach(a->life));
  REQUIRE(!lifecollect(a->life, 1, &result) && result.code == 42);
}
static void
pendingtest(Setid set)
{
  Actor  a, b, c;
  Rqid   ids[3], single;
  Rqrecv first, second, next;
  Msg    reply;
  u32    i;
  actor(&a, set, 50, 16);
  actor(&b, set, 51, 2);
  actor(&c, set, 52, 1);
  for (i = 0; i < 4; i++)
    reply.words[i] = 0;
  for (i = 0; i < 3; i++)
    ids[i] = submit(&a, &b, i + 1);
  REQUIRE(!rqrecv(&b.mail, &first) && first.message.words[0] == 1);
  REQUIRE(!rqdefer(&b.mail, first.request));
  REQUIRE(!rqrecv(&b.mail, &second) && second.message.words[0] == 2);
  REQUIRE(rqdefer(&b.mail, second.request) == -ERR_BUSY && b.mail.deferred == 1);
  REQUIRE(rqrecv(&b.mail, &next) == -ERR_BUSY);
  REQUIRE(rqmailconfigure(&b.mail, 16) == -ERR_STATE && b.mail.limit == 2);
  REQUIRE(!rqreply(&b.mail, second.request, &reply));
  REQUIRE(!rqrecv(&b.mail, &next) && next.message.words[0] == 3);
  REQUIRE(!rqreply(&b.mail, next.request, &reply) && !rqreply(&b.mail, first.request, &reply));
  for (i = 0; i < 3; i++)
    REQUIRE(!rqack(ids[i], a.image, ACK_RESULT | ACK_RELEASE));
  single = submit(&a, &c, 4);
  REQUIRE(!rqrecv(&c.mail, &first) && rqdefer(&c.mail, first.request) == -ERR_BUSY);
  REQUIRE(!rqreply(&c.mail, first.request, &reply));
  REQUIRE(!rqack(single, a.image, ACK_RESULT | ACK_RELEASE));
  endactor(&a);
  endactor(&b);
  endactor(&c);
}
void
createtest(void)
{
  Setid        set;
  Set          beforepolicy, group;
  Pgstat       before, after;
  Rtable      *parent, *child, *grandchild;
  Createpolicy policy, inherited;
  Createarg    args;
  Queryrec     records[4];
  Handle       pids[3], cap, childcap, grandcap, extra, fresh, failed;
  u64          next;
  u32          i, written;
  pgstat(&before);
  REQUIRE(!setcreate(256, 512, 8, &set));
  REQUIRE(!procnew(set, 1, 128, 256, 16, &pids[0]));
  REQUIRE(!procnew(set, pids[0], 64, 128, 8, &pids[1]));
  REQUIRE(!procnew(set, pids[1], 32, 64, 4, &pids[2]));
  parent = proctable(pids[0]);
  child = proctable(pids[1]);
  grandchild = proctable(pids[2]);
  REQUIRE(parent && child && grandchild && !setstat(set, &beforepolicy));
  policy.set = set;
  policy.memlimit = 128;
  policy.exec_limit = 256;
  policy.resources = 16;
  policy.pending = 2;
  policy.rights = CREATE_RIGHTS | LOCAL_GRANT;
  REQUIRE(!localcreate(parent, &policy, &cap));
  for (i = 0; i < sizeof(args); i++)
    ((u8 *) &args)[i] = 0;
  args.memlimit = 64;
  args.exec_limit = 128;
  args.resources = 8;
  args.pending = 1;
  REQUIRE(!localchildcreate(parent, cap, child, &args, &childcap));
  REQUIRE(!localcreator(child, childcap, &inherited));
  REQUIRE(inherited.rights == CREATE_USE && inherited.memlimit == 64 &&
          inherited.exec_limit == 128 && inherited.resources == 8 && inherited.pending == 1 &&
          inherited.set.index == set.index && inherited.set.generation == set.generation);
  REQUIRE(!localquery(parent, 0, records, 4, &written, &next) && written == 3 && !next);
  REQUIRE(records[2].data[0] == (u64) cap && records[2].data[1] == LOCAL_CREATE);
  for (i = 3; i < 20; i++)
    REQUIRE(!records[2].data[i]);
  REQUIRE(localnative(parent, cap) == 0 && localfree(parent, cap) == -ERR_BAD_HANDLE);
  args.pending = 2;
  REQUIRE(localcreatecheck(child, childcap, &args, &inherited) == -ERR_DENIED);
  args.pending = 1;
  args.memlimit = 32;
  args.exec_limit = 64;
  args.resources = 4;
  REQUIRE(!localchildcreate(child, childcap, grandchild, &args, &grandcap));
  REQUIRE(!localcreator(grandchild, grandcap, &inherited));
  REQUIRE(inherited.memlimit == 32 && inherited.resources == 4 && inherited.rights == CREATE_USE);
  REQUIRE(!setstat(set, &group) && group.leases == 3 && group.instances == beforepolicy.instances &&
          group.charged == beforepolicy.charged && group.limit == beforepolicy.limit &&
          group.refs == beforepolicy.refs + 3 && !acctcheck());
  REQUIRE(!localrevoke(parent, cap));
  clean(pids[0]);
  REQUIRE(!localcreatecheck(child, childcap, &args, &inherited));
  clean(pids[1]);
  REQUIRE(!localcreator(grandchild, grandcap, &inherited));
  REQUIRE(!localcreate(grandchild, &inherited, &extra));
  REQUIRE(!setstat(set, &beforepolicy));
  failed = 77;
  REQUIRE(localcreate(grandchild, &inherited, &failed) == -ERR_CAPACITY && !failed);
  REQUIRE(!setstat(set, &group) && group.refs == beforepolicy.refs && group.leases == 2);
  REQUIRE(!localrevoke(grandchild, grandcap) && !localcreate(grandchild, &inherited, &fresh));
  REQUIRE(handleindex(fresh) == handleindex(grandcap) && fresh != grandcap);
  REQUIRE(localcreator(grandchild, grandcap, &policy) == -ERR_BAD_HANDLE);
  clean(pids[2]);
  REQUIRE(!setstat(set, &group) && !group.leases && !group.charged && !group.instances &&
          !acctcheck());
  pendingtest(set);
  REQUIRE(!setstat(set, &group) && !group.leases && !group.charged && !group.instances &&
          !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("CREATECHECK children=");
  puthex(2);
  putstr(" pending=");
  puthex(2);
  putstr(" leases=");
  puthex(group.leases);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
