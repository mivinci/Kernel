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
static u32
testredo(void)
{
  static const u64         targets[] = {65, 66, 0, 18, 0};
  static const u32         sources[] = {3, 4, 0, 2, 0};
  static const Fslayout    layout = {1024, 1, 64, 65, 66, 67, 99, 1, 4096, 1024, 1};
  static const Fsgeometry  device = {1024 * 4096, 512, 4096, 0, 0, 1, 4096, 0};
  static const Journalmark mark = {47, 1, 11, 12};
  static const Journalmark empty = {47, 0, 0, 0};
  static const Journalhead head = {47, {65, 66}, 1, 2, 11, 12};
  Journalio                io, old, first;
  Journalplan              plan;
  u32                      i, edge;
  for (edge = 0; edge <= ARRAY_SIZE(targets); edge++) {
    u64 serial = journal.serial;
    metadata[0].block = 65;
    metadata[1].block = 65;
    CHECK(journalrecover(&journal, &layout, &device, &mark, &head, metadata, 2, 12, bytes[2],
                         NULL) == -ERR_STATE);
    metadata[1].block = 66;
    CHECK(
        journalrecover(&journal, &layout, &device, &mark, &head, metadata, 2, 12, bytes[3], NULL) ==
            -ERR_STATE &&
        !journalrecover(&journal, &layout, &device, &mark, &head, metadata, 2, 12, bytes[2], NULL));
    metadata[0].block = 901;
    for (i = 0; i < ARRAY_SIZE(targets); i++) {
      CHECK(journal.durable && journal.last == 7 && journalrelease(&journal) == -ERR_STATE &&
            !journalnext(&journal, &io) && io.txid == 47 && io.serial == serial + i + 1 &&
            io.block == targets[i] && io.bytes == (sources[i] ? bytes[sources[i]] : NULL) &&
            io.kind == (sources[i] ? JOURNAL_WRITE : JOURNAL_FLUSH));
      if (!i) {
        metadata[0].block = 65;
        if (edge)
          CHECK(journalcomplete(&journal, &first, 1) == -ERR_STALE && journal.active);
        copyio(&io, &first);
      }
      if (i == edge) {
        copyio(&io, &old);
        old.serial++;
        CHECK(!journalstop(&journal) && journalrelease(&journal) == -ERR_WOULD_BLOCK &&
              journalrecover(&journal, &layout, &device, &mark, &head, metadata, 2, 12, bytes[2],
                             NULL) == -ERR_STATE &&
              journalrecover(&journal, &layout, &device, &empty, NULL, NULL, 0, 0, NULL, NULL) ==
                  -ERR_STATE &&
              journalcomplete(&journal, &old, 1) == -ERR_STALE &&
              !journalcomplete(&journal, &io, 1) &&
              journalrecover(&journal, &layout, &device, &mark, &head, metadata, 2, 12, bytes[2],
                             NULL) == -ERR_STATE &&
              !journalrelease(&journal) && journal.phase == JOURNAL_STOPPED && journal.last == 7 &&
              journalnext(&journal, &io) == -ERR_STATE);
        break;
      }
      CHECK(!journalcomplete(&journal, &io, 1));
    }
  }
  CHECK(journal.phase == JOURNAL_DONE && !journalrelease(&journal) &&
        journal.phase == JOURNAL_IDLE && journal.last == 47);
  metadata[0].block = 65;
  plan.header = bytes[0];
  plan.committed = bytes[1];
  plan.empty = bytes[2];
  plan.metadata = metadata;
  plan.data = NULL;
  plan.metadata_count = 2;
  plan.data_count = 0;
  copyio(&io, &old);
  CHECK(!journalbegin(&journal, &plan) && !journalnext(&journal, &io) && io.txid == 48 &&
        io.serial == old.serial + 1 && journalcomplete(&journal, &old, 1) == -ERR_STALE &&
        !journalstop(&journal) && !journalcomplete(&journal, &io, 1) && !journalrelease(&journal));
  return i;
}
void
fsjournaltest(void)
{
  Journalio io, wrong;
  u32       i, edge, redo;
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
  redo = testredo();
  putstr("FSJOURNALCHECK operations=");
  puthex(14);
  putstr(" failures=");
  puthex(14);
  putstr(" timeouts=");
  puthex(14);
  putstr(" redo=");
  puthex(redo);
  putstr(" state_bytes=");
  puthex(sizeof(journal));
  putstr("\n");
}
