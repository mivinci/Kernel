#include "ktest.h"
#include <life.h>
#include <diag.h>
static Lifeid retained[LIFES];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("life responsibility");                                                                \
  } while (0)
void
lifetest(void)
{
  Setid      group;
  Set        set;
  Lifeid     fresh;
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
  REQUIRE(!setstat(group, &set) && !set.instances && !setclose(group));
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("LIFECHECK retained=");
  puthex(LIFES);
  putstr(" reused=");
  puthex(1);
  putstr(" instances=");
  puthex(set.instances);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
