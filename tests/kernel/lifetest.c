#include "ktest.h"
#include <life.h>
#include <diag.h>
#include <abi/signal.h>
static Lifeid retained[LIFES];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("life responsibility");                                                                \
  } while (0)
static void
jobs(void)
{
  Setid      budget;
  Set        account;
  Lifeid     identity, selected, fresh;
  Life       state;
  Group      before, after;
  Childsel   choice;
  Liferesult stopped, result;
  Lifefault  fault;
  u32        matched;
  Handle     pid = handlepack(HANDLE_PROCESS, 1000, 1);
  REQUIRE(!setcreate(64, 128, 1, &budget) && !lifecreate(budget, pid, 1, &identity) &&
          !lifebootgroup(identity) && !lifepublish(identity));
  REQUIRE(!lifestat(identity, &state) && !groupstat(state.group, &before));
  choice.value = pid;
  choice.kind = WAIT_SELECT_PID;
  choice.reserved = 0;
  REQUIRE(!lifestop(identity, SIGNAL_STOP) && !lifestop(identity, SIGNAL_TSTP));
  REQUIRE(!lifeselect(1, &choice, 0, 0, &selected, &matched) && matched && !selected.generation);
  REQUIRE(!lifeselect(1, &choice, 0, WAIT_WUNTRACED, &selected, &matched) && matched &&
          selected.generation == identity.generation && selected.index == identity.index);
  REQUIRE(lifetake(identity, 1, WAIT_WCONTINUED, &result) == -ERR_WOULD_BLOCK && !result.pid &&
          !result.sequence && !result.reason && !result.code);
  REQUIRE(!lifetake(identity, 1, WAIT_WUNTRACED, &stopped) && stopped.pid == pid &&
          stopped.sequence == 2 && stopped.reason == LIFE_STOPPED && stopped.code == SIGNAL_STOP);
  REQUIRE(!lifestat(identity, &state) && state.state == LIFE_LIVE && state.stopped &&
          state.waitref && state.instance && !state.reason && !state.code &&
          !groupstat(state.group, &after) && before.refs == after.refs &&
          before.members == after.members && before.processes == after.processes);
  REQUIRE(lifetake(identity, 1, WAIT_WUNTRACED, &result) == -ERR_WOULD_BLOCK);
  REQUIRE(!lifecontinue(identity) && !lifecontinue(identity) &&
          !lifetake(identity, 1, WAIT_WCONTINUED, &result) && result.sequence == 3 &&
          result.reason == LIFE_CONTINUED && !result.code);
  REQUIRE(!lifestop(identity, SIGNAL_TTIN) && !lifecontinue(identity) &&
          !lifestop(identity, SIGNAL_TTOU));
  REQUIRE(lifetake(identity, 1, WAIT_WCONTINUED, &result) == -ERR_WOULD_BLOCK);
  fault.pc = 0x10000;
  fault.address = 0x20000;
  fault.cause = 3;
  REQUIRE(!lifeend(identity, LIFE_FAULT, SIGNAL_SEGV, &fault) &&
          !lifetake(identity, 1, 0, &result) && result.sequence == 7 &&
          result.reason == LIFE_FAULT && result.code == SIGNAL_SEGV && stopped.sequence == 2 &&
          stopped.reason == LIFE_STOPPED && stopped.code == SIGNAL_STOP);
  REQUIRE(!lifestat(identity, &state) && !state.stopped && !state.waitref && !state.instance &&
          state.fault.pc == fault.pc && state.fault.address == fault.address &&
          state.fault.cause == fault.cause && !lifedetach(identity));
  REQUIRE(lifestat(identity, &state) == -ERR_STALE);
  REQUIRE(!lifecreate(budget, handlepack(HANDLE_PROCESS, 1001, 1), 1, &fresh) &&
          !lifestat(fresh, &state) && !state.stopped && !state.sequence && !lifecancel(fresh) &&
          !lifedetach(fresh) && !lifecheck() && !groupcheck());
  REQUIRE(!setstat(budget, &account) && !account.instances && !account.charged &&
          !setclose(budget));
}
void
lifetest(void)
{
  Setid      group;
  Set        set;
  Lifeid     fresh, parent, child, live, selected;
  Childsel   choice;
  Handle     ppid, cpid;
  u32        matched;
  Life       old;
  Liferesult result;
  Pgstat     before, after;
  u32        i;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, LIFES, &group));
  for (i = 0; i < LIFES; i++) {
    REQUIRE(!lifecreate(group, handlepack(HANDLE_PROCESS, i + 1, 1), 1, &retained[i]));
    REQUIRE(!lifepublish(retained[i]) && !lifeend(retained[i], LIFE_EXIT, i & 255, NULL));
    REQUIRE(lifeend(retained[i], LIFE_SIGNAL, 9, NULL) == -ERR_STATE);
    REQUIRE(!lifedetach(retained[i]));
  }
  REQUIRE(!setstat(group, &set) && set.instances == LIFES);
  REQUIRE(lifecreate(group, handlepack(HANDLE_PROCESS, LIFES + 1, 1), 1, &fresh) == -ERR_CAPACITY);
  REQUIRE(!lifehold(retained[0], LIFE_CLEANUP) && !lifecollect(retained[0], 1, &result));
  REQUIRE(result.pid == handlepack(HANDLE_PROCESS, 1, 1) && result.code == 0);
  REQUIRE(!lifestat(retained[0], &old) && old.cleanup == 1 && old.instance == 0);
  REQUIRE(lifecreate(group, handlepack(HANDLE_PROCESS, LIFES + 1, 1), 1, &fresh) == -ERR_CAPACITY);
  REQUIRE(!lifedrop(retained[0], LIFE_CLEANUP));
  REQUIRE(!lifecreate(group, handlepack(HANDLE_PROCESS, LIFES + 1, 1), 1, &fresh));
  REQUIRE(fresh.index == retained[0].index && fresh.generation > retained[0].generation);
  REQUIRE(lifestat(retained[0], &old) == -ERR_STALE);
  REQUIRE(!lifecancel(fresh) && !lifedetach(fresh));
  for (i = 1; i < LIFES; i++) {
    REQUIRE(!lifestat(retained[i], &old) && old.pid == handlepack(HANDLE_PROCESS, i + 1, 1));
    REQUIRE(!lifecollect(retained[i], 1, &result) && result.code == (i & 255));
  }
  ppid = handlepack(HANDLE_PROCESS, LIFES + 2, 1);
  cpid = handlepack(HANDLE_PROCESS, 1, 2);
  REQUIRE(!lifecreate(group, ppid, 1, &parent) && !lifepublish(parent));
  REQUIRE(!lifecreate(group, cpid, ppid, &child) && !lifepublish(child));
  REQUIRE(!lifecreate(group, handlepack(HANDLE_PROCESS, 1, 3), ppid, &live) && !lifepublish(live));
  REQUIRE(!lifeend(child, LIFE_EXIT, 42, NULL));
  REQUIRE(!lifeend(parent, LIFE_EXIT, 0, NULL));
  /* Logical collection precedes both physical parent rewriting and run cleanup. */
  REQUIRE(!lifestat(child, &old) && old.parent == 1);
  REQUIRE(!lifestat(live, &old) && old.parent == 1);
  choice.value = cpid;
  choice.kind = WAIT_SELECT_PID;
  choice.reserved = 0;
  REQUIRE(!lifeselect(1, &choice, 0, 0, &selected, &matched) && matched);
  REQUIRE(selected.generation == child.generation && selected.index == child.index);
  REQUIRE(lifecollect(child, ppid, &result) == -ERR_DENIED);
  REQUIRE(!lifecollect(child, 1, &result) && result.code == 42);
  REQUIRE(!lifedetach(child));
  REQUIRE(!lifedetach(parent) && !lifecollect(parent, 1, &result));
  REQUIRE(!lifecreate(group, handlepack(HANDLE_PROCESS, LIFES + 3, 1), 1, &fresh));
  REQUIRE(!lifestat(live, &old) && old.parent == 1);
  REQUIRE(!lifecancel(fresh) && !lifedetach(fresh));
  REQUIRE(!lifeend(live, LIFE_EXIT, 7, NULL) && !lifecollect(live, 1, &result));
  REQUIRE(!lifedetach(live) && !lifecheck());
  REQUIRE(!setstat(group, &set) && !set.instances && !setclose(group));
  jobs();
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("LIFECHECK retained=");
  puthex(LIFES);
  putstr(" reused=");
  puthex(1);
  putstr(" jobs=");
  puthex(1);
  putstr(" instances=");
  puthex(set.instances);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
