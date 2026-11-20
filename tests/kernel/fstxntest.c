#include "ktest.h"
#include "../../sbin/fs/txn.h"
#include <diag.h>
static Txn        txn;
static Cache      cache;
static Cacheblock blocks[2];
static Cachekey   keys[2];
static u8         snapshots[2][CACHE_BYTES], records[3][JOURNAL_BYTES];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FSTXNFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("FS captured cache transaction");                                                      \
    }                                                                                              \
  } while (0)
static void
prepare(void)
{
  Cacheio      read;
  u8          *destination, byte;
  volatile u8 *to;
  u32          i, j;
  CHECK(!cacheinit(&cache, blocks, 2, 1024) && !txninit(&txn, &cache, 7));
  for (i = 0; i < 2; i++) {
    keys[i].generation = keys[i].index = keys[i].reserved = 0;
    CHECK(!cacheget(&cache, i ? 200 : 65, i ? CACHE_DATA : CACHE_META, &keys[i]) &&
          !cachereadbegin(&cache, &keys[i], &read, &destination));
    to = destination;
    for (j = 0; j < CACHE_BYTES; j++)
      to[j] = 0;
    byte = (u8) (i ? 65 : 66);
    CHECK(!cachecomplete(&cache, &read, 1) && !cacheedit(&cache, &keys[i], 0, &byte, 1));
  }
  CHECK(!txnprepare(&txn, &keys[0], 1, &keys[1], 1, snapshots) && txn.count == 2 &&
        snapshots[0][0] == 66 && snapshots[1][0] == 65 &&
        cachecomplete(&cache, &txn.captures[0], 1) == -ERR_STATE);
  for (i = 0; i < 3; i++)
    for (j = 0; j < JOURNAL_BYTES; j++)
      records[i][j] = (u8) (i + 1);
}
static void
start(void)
{
  CHECK(!txnstart(&txn, records[0], records[1], records[2]));
}
static void
held(void)
{
  CHECK(blocks[0].io == CACHE_CAPTURE && blocks[1].io == CACHE_CAPTURE && blocks[0].dirty &&
        blocks[1].dirty && txn.count == 2);
}
void
fstxntest(void)
{
  Journalio io;
  Cachekey  missing = {0, 0, 0};
  u8        byte = 90;
  u32       i, edge, count = 0;
  prepare();
  CHECK(!cacheedit(&cache, &keys[0], 0, &byte, 1) && !cacheput(&cache, &keys[0]) &&
        !cacheput(&cache, &keys[1]) &&
        cacheget(&cache, 300, CACHE_DATA, &missing) == -ERR_CAPACITY);
  start();
  while (txn.journal.phase != JOURNAL_DONE) {
    held();
    CHECK(!txnnext(&txn, &io) && txnrelease(&txn) == -ERR_WOULD_BLOCK);
    if (txn.journal.phase == JOURNAL_HOME_WRITE)
      CHECK(io.block == 65 && io.bytes[0] == 66 && blocks[0].data[0] == 90);
    CHECK(!txncomplete(&txn, &io, 1));
    count++;
  }
  held();
  CHECK(count == 11 && !txnrelease(&txn) && txn.phase == TXN_IDLE && txn.journal.last == 8 &&
        !blocks[0].io && !blocks[1].io && blocks[0].dirty && !blocks[1].dirty &&
        !cacheget(&cache, 65, CACHE_META, &keys[0]) &&
        !txnprepare(&txn, &keys[0], 1, 0, 0, snapshots) && snapshots[0][0] == 90 &&
        !txnrelease(&txn) && blocks[0].dirty && !blocks[0].io && txn.journal.last == 8 &&
        !cacheput(&cache, &keys[0]));
  for (edge = 0; edge < 11; edge++) {
    prepare();
    start();
    for (i = 0; i <= edge; i++) {
      CHECK(!txnnext(&txn, &io) && !txncomplete(&txn, &io, i != edge));
      held();
    }
    CHECK(!txnrelease(&txn) && txn.phase == TXN_STOPPED && txn.journal.last == 7 && !blocks[0].io &&
          !blocks[1].io && blocks[0].dirty && blocks[1].dirty);
  }
  for (edge = 0; edge < 11; edge++) {
    prepare();
    start();
    for (i = 0; i < edge; i++)
      CHECK(!txnnext(&txn, &io) && !txncomplete(&txn, &io, 1));
    CHECK(!txnnext(&txn, &io) && !txnstop(&txn) && txnrelease(&txn) == -ERR_WOULD_BLOCK);
    held();
    CHECK(!txncomplete(&txn, &io, 1) && !txnrelease(&txn) && txn.phase == TXN_STOPPED &&
          txn.journal.last == 7 && !blocks[0].io && !blocks[1].io && blocks[0].dirty &&
          blocks[1].dirty);
  }
  putstr("FSTXNCHECK operations=");
  puthex(count);
  putstr(" failures=");
  puthex(11);
  putstr(" timeouts=");
  puthex(11);
  putstr(" newer=");
  puthex(1);
  putstr(" state_bytes=");
  puthex(sizeof(txn));
  putstr("\n");
}
