#include "ktest.h"
#include <image.h>
#include <proc.h>
#include <diag.h>
static Imageid retained[IMAGES];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("IMAGEFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("image lifetime");                                                                     \
    }                                                                                              \
  } while (0)
void
imagetest(void)
{
  Setid   set;
  Set     group;
  Lifeid  oldlife, freshlife;
  Life    life;
  Image   image;
  Imageid fresh, stale;
  Handle  oldpid = (2 + (1) * 64 + (PROCS - 1)),
         pid = (2 + (2) * 64 + (PROCS - 1)), constructed;
  Liferesult result;
  u32        i;
  Pgstat     before, after;
  int        error;
  REQUIRE(!setcreate(128, 256, 4, &set));
  REQUIRE(!lifecreaterun(set, oldpid, 1, PROCS - 1, &oldlife));
  for (i = 0; i < IMAGES; i++) {
    REQUIRE(!imagecreate(oldlife, 7, &retained[i]));
    REQUIRE(!imagestat(retained[i], &image) && image.number == (u64) i + 1);
    REQUIRE(!imagehold(retained[i], IMAGE_CLEANUP));
    REQUIRE(!imageend(retained[i], IMAGE_TIMEOUT) && !imagedetach(retained[i]));
  }
  REQUIRE(imagecreate(oldlife, 7, &fresh) == -ERR_CAPACITY);
  pgstat(&before);
  REQUIRE(procnew(set, oldpid, 128, 256, LOCALS, &constructed) == -ERR_CAPACITY && !constructed);
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free);
  REQUIRE(!setstat(set, &group) && group.instances == 1 && !group.charged);
  REQUIRE(!lifestat(oldlife, &life) && life.imagegen == IMAGES && life.cleanup == IMAGES);
  stale = retained[IMAGES - 1];
  REQUIRE(!imagedrop(stale, IMAGE_CLEANUP));
  REQUIRE(!procnew(set, oldpid, 128, 256, LOCALS, &constructed));
  REQUIRE(!procdestroy(constructed));
  for (i = 0; i < 32; i++) {
    error = procclean(constructed);
    REQUIRE(error >= 0);
    if (!error)
      break;
  }
  REQUIRE(i < 32);
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free);
  REQUIRE(!setstat(set, &group) && group.instances == 1 && !group.charged);
  REQUIRE(!imagecreate(oldlife, 7, &fresh) && fresh.index == stale.index &&
          fresh.generation != stale.generation);
  REQUIRE(!imagestat(fresh, &image) && image.number == IMAGES + 1 && image.pid == oldpid);
  REQUIRE(imagestat(stale, &image) == -ERR_STALE);
  REQUIRE(!imageend(fresh, IMAGE_ABORT) && !imagedetach(fresh));
  for (i = 1; i + 1 < IMAGES; i++)
    REQUIRE(!imagedrop(retained[i], IMAGE_CLEANUP));
  REQUIRE(!lifecancel(oldlife) && !lifedetach(oldlife));
  REQUIRE(!lifestat(oldlife, &life) && life.run == PG_NONE && life.cleanup == 1 && !life.instance);
  REQUIRE(!lifecreaterun(set, pid, 1, PROCS - 1, &freshlife) && freshlife.index != oldlife.index);
  REQUIRE(!imagecreate(freshlife, 0, &fresh) && !lifepublish(freshlife) && !imageactivate(fresh));
  REQUIRE(!imagestat(fresh, &image) && image.pid == pid && image.number == 1);
  REQUIRE(!imagestat(retained[0], &image) && image.pid == oldpid && image.number == 1 &&
          image.state == IMAGE_ENDED && image.reason == IMAGE_TIMEOUT);
  REQUIRE(imagehold(retained[0], IMAGE_OBSERVER) == -ERR_STATE);
  REQUIRE(!lifeend(freshlife, LIFE_EXIT, 42, NULL) && !imageend(fresh, IMAGE_EXIT) &&
          !imagedetach(fresh) && !lifedetach(freshlife));
  REQUIRE(!lifecollect(freshlife, 1, &result) && result.code == 42);
  REQUIRE(!imagedrop(retained[0], IMAGE_CLEANUP));
  REQUIRE(lifestat(oldlife, &life) == -ERR_STALE);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  putstr("IMAGECHECK capacity=");
  puthex(IMAGES);
  putstr(" next=");
  puthex(IMAGES + 1);
  putstr(" retained=");
  puthex(1);
  putstr(" charged=");
  puthex(group.charged);
  putstr("\n");
}
