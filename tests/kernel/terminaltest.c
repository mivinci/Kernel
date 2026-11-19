#include "ktest.h"
#include <terminal.h>
#include <diag.h>
static Handle retained[TERMINALS];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("TERMINALFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("terminal relationships");                                                             \
    }                                                                                              \
  } while (0)

void
terminaltest(void)
{
  Setid      set;
  Set        budget;
  Lifeid     identity;
  Life       life;
  Imageid    old, fresh;
  Image      image;
  Terminal   snapshot;
  Group      relation;
  Liferesult result;
  Pgstat     before, after;
  Handle     pid = handlepack(HANDLE_PROCESS, 1, PROCS - 1);
  Handle     sid, foreign, first, second, alien, terminal, replacement, full = 99;
  u32        i;
  pgstat(&before);
  CHECK(!setcreate(128, 256, 1, &set) && !lifecreate(set, pid, 1, &identity));
  CHECK(!imagecreate(identity, 0, &old) && !terminalnew(old, &terminal));
  CHECK(!terminalstat(terminal, &snapshot) && snapshot.epoch == 1 && !snapshot.session &&
        !snapshot.foreground && snapshot.refs == 1);
  CHECK(!sessioncreate(pid, &sid) &&
        !sessioncreate(handlepack(HANDLE_PROCESS, 1, PROCS - 2), &foreign));
  CHECK(!groupcreate(pid, sid, &first) &&
        !groupcreate(handlepack(HANDLE_PROCESS, 1, PROCS - 3), sid, &second) &&
        !groupcreate(handlepack(HANDLE_PROCESS, 1, PROCS - 2), foreign, &alien));
  CHECK(terminalset(terminal, first, 1) == -ERR_STATE);
  CHECK(!terminalbind(terminal, sid) && terminalbind(terminal, sid) == -ERR_STATE);
  CHECK(terminalset(terminal, alien, 1) == -ERR_DENIED);
  CHECK(!terminalset(terminal, first, 1) && !terminalset(terminal, second, 2));
  CHECK(terminalset(terminal, first, 2) == -ERR_STALE);
  CHECK(!terminalstat(terminal, &snapshot) && snapshot.epoch == 3 && snapshot.foreground == second);
  CHECK(!terminalset(terminal, second, 3) && !terminalhold(terminal));
  CHECK(!groupstat(second, &relation) && relation.refs == 2);
  CHECK(!lifepublish(identity) && !imageactivate(old) && !imageend(old, IMAGE_EXEC) &&
        !imagedetach(old));
  CHECK(terminalhold(terminal) == -ERR_STALE && terminalstat(terminal, &snapshot) == -ERR_STALE &&
        terminalset(terminal, first, 4) == -ERR_STALE);
  CHECK(!imagestat(old, &image) && image.state == IMAGE_ENDED && image.cleanup == 1 &&
        !image.owner);
  CHECK(!imagecreate(identity, 0, &fresh) && !imageactivate(fresh) &&
        !terminalnew(fresh, &replacement) && replacement != terminal);
  CHECK(terminalbind(replacement, sid) == -ERR_BUSY);
  CHECK(!terminaldrop(terminal) && !imagestat(old, &image));
  CHECK(!terminaldrop(terminal) && imagestat(old, &image) == -ERR_STALE);
  CHECK(!terminalbind(replacement, sid) && !terminalset(replacement, first, 1));
  CHECK(!terminaldrop(replacement));
  for (i = 0; i < TERMINALS; i++)
    CHECK(!terminalnew(fresh, &retained[i]));
  CHECK(terminalnew(fresh, &full) == -ERR_CAPACITY && full == 99);
  terminal = retained[0];
  CHECK(!terminaldrop(terminal) && !terminalnew(fresh, &retained[0]) && retained[0] != terminal &&
        terminaldrop(terminal) == -ERR_STALE);
  for (i = 0; i < TERMINALS; i++)
    CHECK(!terminaldrop(retained[i]));
  CHECK(!imagestat(fresh, &image) && !image.cleanup);
  CHECK(!lifeend(identity, LIFE_EXIT, 42, NULL) && !imageend(fresh, IMAGE_EXIT) &&
        !imagedetach(fresh) && !lifedetach(identity) && !lifecollect(identity, 1, &result) &&
        result.code == 42 && lifestat(identity, &life) == -ERR_STALE);
  CHECK(!groupdrop(first) && !groupdrop(second) && !groupdrop(alien) && !groupdrop(sid) &&
        !groupdrop(foreign) && !groupcheck());
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
        !acctcheck());
  putstr("TERMINALCHECK epoch=");
  puthex(4);
  putstr(" stale=");
  puthex(1);
  putstr(" exec=");
  puthex(1);
  putstr(" capacity=");
  puthex(TERMINALS);
  putstr(" reclaimed=");
  puthex(1);
  putstr(" charged=");
  puthex(budget.charged);
  putstr("\n");
}
