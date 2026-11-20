#include "ktest.h"
#include "../../sbin/fs/journal.h"
#include <diag.h>
static Journal      journal;
static u8           bytes[7][JOURNAL_BYTES];
static Journalblock metadata[2], data[2];
static const u32    phases[14] = {1, 1, 2, 3, 3, 3, 4, 5, 6, 7, 7, 8, 9, 10};
static const u32    kinds[14] = {1, 1, 2, 1, 1, 1, 2, 1, 2, 1, 1, 2, 1, 2};
static const u64    blocks[14] = {200, 201, 0, 1, 2, 3, 0, 18, 0, 65, 66, 0, 18, 0};
static const u32    sources[14] = {5, 6, 0, 0, 3, 4, 0, 1, 0, 3, 4, 0, 2, 0};
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FSJOURNALFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("FS journal order core");                                                              \
    }                                                                                              \
  } while (0)
static void
begin(void)
{
  Journalplan plan;
  u32         i, j;
  CHECK(!journalinit(&journal, 1024, 7));
  for (i = 0; i < 7; i++)
    for (j = 0; j < JOURNAL_BYTES; j++)
      bytes[i][j] = (u8) (i + 1);
  for (i = 0; i < 2; i++) {
    metadata[i].block = 65 + i;
    metadata[i].bytes = bytes[3 + i];
    data[i].block = 200 + i;
    data[i].bytes = bytes[5 + i];
  }
  plan.header = bytes[0];
  plan.committed = bytes[1];
  plan.empty = bytes[2];
  plan.metadata = metadata;
  plan.data = data;
  plan.metadata_count = plan.data_count = 2;
  CHECK(!journalbegin(&journal, &plan));
}
static void
copyio(const Journalio *from, Journalio *to)
{
  to->serial = from->serial;
  to->txid = from->txid;
  to->block = from->block;
  to->bytes = from->bytes;
  to->kind = from->kind;
  to->reserved = from->reserved;
}
static void
next(u32 index, Journalio *io)
{
  CHECK(journal.phase == phases[index] && journal.last == 7 &&
        journal.durable == (u32) (index >= 9) && !journalnext(&journal, io) &&
        io->serial == index + 1 && io->txid == 8 && io->kind == kinds[index] &&
        io->block == blocks[index]);
  CHECK(io->kind == JOURNAL_FLUSH ? !io->bytes : io->bytes == bytes[sources[index]]);
}
void
fsjournaltest(void)
{
  Journalio io, wrong;
  u32       i, edge;
  begin();
  /* The engine owns a copied descriptor view, not these caller arrays. */
  data[0].block = 900;
  metadata[0].block = 901;
  for (i = 0; i < 14; i++) {
    next(i, &io);
    copyio(&io, &wrong);
    wrong.serial++;
    CHECK(journalnext(&journal, &wrong) == -ERR_BUSY &&
          journalcomplete(&journal, &wrong, 1) == -ERR_STALE && journal.active &&
          !journalcomplete(&journal, &io, 1));
  }
  CHECK(journal.phase == JOURNAL_DONE && journal.durable && journal.last == 7 &&
        !journalrelease(&journal) && journal.phase == JOURNAL_IDLE && journal.last == 8 &&
        !journal.header);
  for (edge = 0; edge < 14; edge++) {
    begin();
    for (i = 0; i <= edge; i++) {
      next(i, &io);
      CHECK(!journalcomplete(&journal, &io, i != edge));
    }
    CHECK(journal.phase == JOURNAL_STOPPED && !journal.active && journal.last == 7 &&
          journalnext(&journal, &io) == -ERR_STATE && !journalrelease(&journal) &&
          journal.phase == JOURNAL_STOPPED && !journal.header && journal.last == 7);
  }
  for (edge = 0; edge < 14; edge++) {
    begin();
    for (i = 0; i < edge; i++) {
      next(i, &io);
      CHECK(!journalcomplete(&journal, &io, 1));
    }
    next(edge, &io);
    copyio(&io, &wrong);
    wrong.txid++;
    CHECK(!journalstop(&journal) && journalrelease(&journal) == -ERR_WOULD_BLOCK &&
          journal.active && journal.header == bytes[0] &&
          journalcomplete(&journal, &wrong, 1) == -ERR_STALE && journal.active &&
          !journalcomplete(&journal, &io, 1) && journal.phase == JOURNAL_STOPPED &&
          !journalrelease(&journal) && !journal.header && journal.last == 7);
  }
  putstr("FSJOURNALCHECK operations=");
  puthex(14);
  putstr(" failures=");
  puthex(14);
  putstr(" timeouts=");
  puthex(14);
  putstr(" state_bytes=");
  puthex(sizeof(journal));
  putstr("\n");
}
