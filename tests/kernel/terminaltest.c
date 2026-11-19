#include "ktest.h"
#include <terminal.h>
#include <proc.h>
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

static void
localcheck(void)
{
  Setid    set;
  Set      budget;
  Handle   parent, child, terminal, full, query, signal, denied, target;
  Procinfo source, destination;
  Rtable  *from, *to;
  Queryrec rows[8];
  Image    state;
  u64      bits, next;
  u32      written, i, found = 0;
  int      error;
  CHECK(!setcreate(256, 512, 2, &set) && !procnew(set, 1, 64, 128, 8, &parent) &&
        !procinfo(parent, &source) && !procnew(set, 1, 64, 128, 8, &child) &&
        !procinfo(child, &destination));
  from = proctable(parent);
  to = proctable(child);
  CHECK(from && to && !terminalnew(source.identity, &terminal));
  CHECK(!localterminal(from, terminal, TERMINAL_RIGHTS | LOCAL_GRANT, TERMINAL_SIGNALS, 0, &full));
  CHECK(localinherit(from, full, LOCAL_FORK) == -ERR_DENIED);
  CHECK(!terminaldrop(terminal));
  CHECK(!localcopy(from, full, to, TERMINAL_QUERY | TERMINAL_FG, 0, 0, LOCAL_FORK | LOCAL_EXEC,
                   &query));
  CHECK(!localcopy(from, full, to, TERMINAL_SIGNAL, (u64) 1 << (SIGNAL_INT - 1), 0, 0, &signal));
  CHECK(!localterminalaccess(to, signal, TERMINAL_SIGNAL, &target, &bits) && target == terminal &&
        bits == ((u64) 1 << (SIGNAL_INT - 1)));
  CHECK(localterminalaccess(to, query, TERMINAL_SIGNAL, &target, &bits) == -ERR_DENIED && !target &&
        !bits);
  CHECK(localcopy(from, full, to, TERMINAL_SIGNAL, (u64) 1 << (SIGNAL_KILL - 1), 0, 0, &denied) ==
            -ERR_DENIED &&
        !denied);
  CHECK(localcopy(to, query, from, TERMINAL_BIND, 0, 0, 0, &denied) == -ERR_DENIED && !denied);
  CHECK(!procdestroy(parent));
  from = NULL;
  while ((error = procclean(parent)) > 0) {
  }
  CHECK(!error && !imagestat(source.identity, &state) && state.state == IMAGE_ENDED);
  CHECK(localterminalaccess(to, query, TERMINAL_QUERY, &target, &bits) == -ERR_STALE && !target &&
        !bits);
  CHECK(!localquery(to, 0, rows, ARRAY_SIZE(rows), &written, &next) && !next);
  for (i = 0; i < written; i++)
    if (rows[i].data[0] == (u64) query) {
      CHECK(rows[i].data[1] == RESOURCE_TERMINAL &&
            rows[i].data[2] == (TERMINAL_QUERY | TERMINAL_FG) && !rows[i].data[3] &&
            rows[i].data[5] == 3 && rows[i].data[6] == (u64) parent &&
            rows[i].data[7] == source.image);
      found++;
    }
  CHECK(found == 1 && !localfree(to, query) && !localfree(to, signal) &&
        imagestat(source.identity, &state) == -ERR_STALE);
  CHECK(!procdestroy(child));
  to = NULL;
  while ((error = procclean(child)) > 0) {
  }
  CHECK(!error && !setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
}

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
  localcheck();
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
  putstr(" local=");
  puthex(1);
  putstr(" charged=");
  puthex(budget.charged);
  putstr("\n");
}
