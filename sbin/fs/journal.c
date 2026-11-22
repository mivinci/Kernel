#include "journal.h"
static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}
static int
external(const Journal *journal, const void *out, u64 bytes)
{
  return separate(journal, sizeof(*journal), out, bytes);
}
static int
home(u64 block, u64 blocks)
{
  return block >= FS_INODE_MAP && block < blocks;
}
static int
hometargets(const Journalblock *metadata, u32 count, u64 blocks)
{
  u32 i, j;
  for (i = 0; i < count; i++) {
    if (!home(metadata[i].block, blocks))
      return 0;
    for (j = 0; j < i; j++)
      if (metadata[i].block == metadata[j].block)
        return 0;
  }
  return 1;
}
static void
copymetadata(Journal *journal, const Journalblock *metadata, u32 count)
{
  u32 i;
  journal->metadata_count = count;
  for (i = 0; i < count; i++) {
    journal->metadata[i].block = metadata[i].block;
    journal->metadata[i].bytes = metadata[i].bytes;
  }
}
static void
clear(Journal *journal)
{
  u32 i;
  journal->txid = 0;
  journal->header = journal->committed = journal->empty = 0;
  journal->metadata_count = journal->data_count = journal->cursor = 0;
  journal->active = journal->durable = 0;
  journal->pending.serial = journal->pending.txid = journal->pending.block = 0;
  journal->pending.bytes = 0;
  journal->pending.kind = journal->pending.reserved = 0;
  for (i = 0; i < JOURNAL_META; i++) {
    journal->metadata[i].block = 0;
    journal->metadata[i].bytes = 0;
  }
  for (i = 0; i < JOURNAL_DATA; i++) {
    journal->data[i].block = 0;
    journal->data[i].bytes = 0;
  }
}
isize
journalinit(Journal *journal, u64 blocks, u64 last)
{
  if (!journal || blocks <= FS_DATA_START || blocks > FS_MAX_BLOCKS)
    return -ERR_INVALID;
  journal->blocks = blocks;
  journal->last = last;
  journal->serial = 0;
  journal->phase = JOURNAL_IDLE;
  journal->reserved[0] = journal->reserved[1] = 0;
  clear(journal);
  return 0;
}
static int
snapshot(const Journal *journal, const Journalplan *plan, const void *bytes)
{
  return external(journal, bytes, JOURNAL_BYTES) &&
         separate(plan, sizeof(*plan), bytes, JOURNAL_BYTES) &&
         separate(plan->metadata, (u64) plan->metadata_count * sizeof(*plan->metadata), bytes,
                  JOURNAL_BYTES) &&
         (!plan->data_count ||
          separate(plan->data, (u64) plan->data_count * sizeof(*plan->data), bytes, JOURNAL_BYTES));
}
static const u8 *
bytesat(const Journalplan *plan, u32 index)
{
  if (!index)
    return plan->header;
  if (index == 1)
    return plan->committed;
  if (index == 2)
    return plan->empty;
  index -= 3;
  if (index < plan->metadata_count)
    return plan->metadata[index].bytes;
  return plan->data[index - plan->metadata_count].bytes;
}
isize
journalbegin(Journal *journal, const Journalplan *plan)
{
  u32 i, j, count;
  if (!journal || journal->blocks <= FS_DATA_START || journal->blocks > FS_MAX_BLOCKS ||
      journal->reserved[0] || journal->reserved[1])
    return -ERR_INVALID;
  if (journal->phase != JOURNAL_IDLE || journal->active || journal->txid)
    return -ERR_STATE;
  if (journal->last == UINT64_MAX)
    return -ERR_OVERFLOW;
  if (!external(journal, plan, sizeof(*plan)) || !plan->metadata_count ||
      plan->metadata_count > JOURNAL_META || plan->data_count > JOURNAL_DATA ||
      !external(journal, plan->metadata, (u64) plan->metadata_count * sizeof(*plan->metadata)) ||
      !separate(plan, sizeof(*plan), plan->metadata,
                (u64) plan->metadata_count * sizeof(*plan->metadata)) ||
      (plan->data_count &&
       (!external(journal, plan->data, (u64) plan->data_count * sizeof(*plan->data)) ||
        !separate(plan, sizeof(*plan), plan->data, (u64) plan->data_count * sizeof(*plan->data)) ||
        !separate(plan->metadata, (u64) plan->metadata_count * sizeof(*plan->metadata), plan->data,
                  (u64) plan->data_count * sizeof(*plan->data)))))
    return -ERR_INVALID;
  if (!hometargets(plan->metadata, plan->metadata_count, journal->blocks))
    return -ERR_INVALID;
  for (i = 0; i < plan->data_count; i++) {
    if (plan->data[i].block < FS_DATA_START || plan->data[i].block >= journal->blocks)
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (plan->data[i].block == plan->data[j].block)
        return -ERR_INVALID;
    for (j = 0; j < plan->metadata_count; j++)
      if (plan->data[i].block == plan->metadata[j].block)
        return -ERR_INVALID;
  }
  count = 3 + plan->metadata_count + plan->data_count;
  for (i = 0; i < count; i++) {
    const u8 *bytes = bytesat(plan, i);
    if (!snapshot(journal, plan, bytes))
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (!separate(bytes, JOURNAL_BYTES, bytesat(plan, j), JOURNAL_BYTES))
        return -ERR_INVALID;
  }
  journal->txid = journal->last + 1;
  journal->header = plan->header;
  journal->committed = plan->committed;
  journal->empty = plan->empty;
  copymetadata(journal, plan->metadata, plan->metadata_count);
  journal->data_count = plan->data_count;
  for (i = 0; i < plan->data_count; i++) {
    journal->data[i].block = plan->data[i].block;
    journal->data[i].bytes = plan->data[i].bytes;
  }
  journal->phase = journal->data_count ? JOURNAL_DATA_WRITE : JOURNAL_DATA_FLUSH;
  journal->cursor = journal->durable = 0;
  return 0;
}
static int
redosnapshots(const Journal *journal, u64 blocks, const Journalblock *metadata, u32 count,
              const u8 *empty)
{
  u32 i, j;
  u64 descriptors = (u64) count * sizeof(*metadata);
  if (blocks <= FS_DATA_START || blocks > FS_MAX_BLOCKS || !count || count > JOURNAL_META ||
      !external(journal, metadata, descriptors) || !external(journal, empty, JOURNAL_BYTES) ||
      !separate(metadata, descriptors, empty, JOURNAL_BYTES) ||
      !hometargets(metadata, count, blocks))
    return 0;
  for (i = 0; i < count; i++) {
    const u8 *bytes = metadata[i].bytes;
    if (!external(journal, bytes, JOURNAL_BYTES) ||
        !separate(metadata, descriptors, bytes, JOURNAL_BYTES) ||
        !separate(empty, JOURNAL_BYTES, bytes, JOURNAL_BYTES))
      return 0;
    for (j = 0; j < i; j++)
      if (!separate(metadata[j].bytes, JOURNAL_BYTES, bytes, JOURNAL_BYTES))
        return 0;
  }
  return 1;
}

static void
redostart(Journal *journal, u64 blocks, u64 txid, const Journalblock *metadata, u32 count,
          const u8 *empty)
{
  /* The previous EMPTY number is immaterial until this log is retired. */
  journalinit(journal, blocks, 0);
  journal->txid = txid;
  journal->empty = empty;
  copymetadata(journal, metadata, count);
  journal->durable = 1;
  journal->phase = JOURNAL_HOME_WRITE;
}

isize
journalredo(Journal *journal, u64 blocks, u64 txid, const Journalblock *metadata, u32 count,
            const u8 *empty)
{
  if (!txid || !redosnapshots(journal, blocks, metadata, count, empty))
    return -ERR_INVALID;
  redostart(journal, blocks, txid, metadata, count, empty);
  return 0;
}

isize
journalrecover(Journal *journal, u64 blocks, const Journalmark *mark, const Journalhead *head,
               const Journalblock *metadata, u32 count, u32 payloadcrc, const u8 *empty,
               int (*classify)(void *, const Journalblock *, u32, u32), void *context)
{
  u32 i;
  if (!journal || !mark || blocks <= FS_DATA_START || blocks > FS_MAX_BLOCKS)
    return -ERR_INVALID;
  if (!mark->state) {
    if (mark->headercrc || mark->payloadcrc)
      return -ERR_STATE;
    return journalinit(journal, blocks, mark->txid);
  }
  if (mark->state != 1 || !mark->txid || !head || head->state != 1 || head->txid != mark->txid ||
      head->headercrc != mark->headercrc || head->payloadcrc != mark->payloadcrc ||
      payloadcrc != mark->payloadcrc || !count || count != head->count || !classify ||
      !redosnapshots(journal, blocks, metadata, count, empty))
    return -ERR_STATE;
  for (i = 0; i < JOURNAL_META; i++) {
    if (i < count) {
      if (head->targets[i] != metadata[i].block)
        return -ERR_STATE;
    } else if (head->targets[i])
      return -ERR_STATE;
  }
  for (i = 0; i < count; i++)
    if (classify(context, metadata, count, i) <= 0)
      return -ERR_STATE;
  redostart(journal, blocks, mark->txid, metadata, count, empty);
  return 0;
}

static int
output(const Journal *journal, const void *out, u64 bytes)
{
  u32 i;
  if (!external(journal, out, bytes) ||
      (journal->header && !separate(journal->header, JOURNAL_BYTES, out, bytes)) ||
      (journal->committed && !separate(journal->committed, JOURNAL_BYTES, out, bytes)) ||
      !separate(journal->empty, JOURNAL_BYTES, out, bytes))
    return 0;
  for (i = 0; i < journal->metadata_count; i++)
    if (!separate(journal->metadata[i].bytes, JOURNAL_BYTES, out, bytes))
      return 0;
  for (i = 0; i < journal->data_count; i++)
    if (!separate(journal->data[i].bytes, JOURNAL_BYTES, out, bytes))
      return 0;
  return 1;
}
isize
journalnext(Journal *journal, Journalio *out)
{
  Journalio io;
  if (!journal || journal->phase < JOURNAL_DATA_WRITE || journal->phase > JOURNAL_EMPTY_FLUSH)
    return -ERR_STATE;
  if (journal->active)
    return -ERR_BUSY;
  if (!output(journal, out, sizeof(*out)))
    return -ERR_INVALID;
  if (journal->serial == UINT64_MAX) {
    journal->phase = JOURNAL_STOPPED;
    return -ERR_OVERFLOW;
  }
  io.serial = journal->serial + 1;
  io.txid = journal->txid;
  io.block = 0;
  io.bytes = 0;
  io.kind = JOURNAL_WRITE;
  io.reserved = 0;
  switch (journal->phase) {
  case JOURNAL_DATA_WRITE:
    io.block = journal->data[journal->cursor].block;
    io.bytes = journal->data[journal->cursor].bytes;
    break;
  case JOURNAL_LOG_WRITE:
    io.block = 1 + journal->cursor;
    io.bytes = journal->cursor ? journal->metadata[journal->cursor - 1].bytes : journal->header;
    break;
  case JOURNAL_MARK_WRITE:
    io.block = 18;
    io.bytes = journal->committed;
    break;
  case JOURNAL_HOME_WRITE:
    io.block = journal->metadata[journal->cursor].block;
    io.bytes = journal->metadata[journal->cursor].bytes;
    break;
  case JOURNAL_EMPTY_WRITE:
    io.block = 18;
    io.bytes = journal->empty;
    break;
  default:
    io.kind = JOURNAL_FLUSH;
    break;
  }
  journal->serial = io.serial;
  journal->pending = io;
  journal->active = 1;
  *out = io;
  return 0;
}
isize
journalcomplete(Journal *journal, const Journalio *ticket, u32 success)
{
  const Journalio *pending;
  if (!journal || !external(journal, ticket, sizeof(*ticket)) || ticket->reserved || success > 1)
    return -ERR_INVALID;
  pending = &journal->pending;
  if (!journal->active || ticket->serial != pending->serial || ticket->txid != pending->txid ||
      ticket->block != pending->block || ticket->bytes != pending->bytes ||
      ticket->kind != pending->kind)
    return -ERR_STALE;
  journal->active = 0;
  if (!success || journal->phase == JOURNAL_STOPPED) {
    journal->phase = JOURNAL_STOPPED;
    return 0;
  }
  switch (journal->phase) {
  case JOURNAL_DATA_WRITE:
    if (++journal->cursor == journal->data_count) {
      journal->phase = JOURNAL_DATA_FLUSH;
      journal->cursor = 0;
    }
    break;
  case JOURNAL_LOG_WRITE:
    if (++journal->cursor == journal->metadata_count + 1) {
      journal->phase = JOURNAL_LOG_FLUSH;
      journal->cursor = 0;
    }
    break;
  case JOURNAL_HOME_WRITE:
    if (++journal->cursor == journal->metadata_count) {
      journal->phase = JOURNAL_HOME_FLUSH;
      journal->cursor = 0;
    }
    break;
  case JOURNAL_MARK_FLUSH:
    journal->durable = 1;
    journal->phase = JOURNAL_HOME_WRITE;
    break;
  default:
    journal->phase++;
    break;
  }
  return 0;
}
isize
journalstop(Journal *journal)
{
  if (!journal || journal->phase < JOURNAL_DATA_WRITE || journal->phase > JOURNAL_STOPPED)
    return -ERR_STATE;
  if (journal->phase == JOURNAL_DONE)
    return -ERR_STATE;
  journal->phase = JOURNAL_STOPPED;
  return 0;
}
isize
journalrelease(Journal *journal)
{
  u32 phase;
  if (!journal || (journal->phase != JOURNAL_DONE && journal->phase != JOURNAL_STOPPED))
    return -ERR_STATE;
  if (journal->active)
    return -ERR_WOULD_BLOCK;
  phase = journal->phase;
  if (phase == JOURNAL_DONE)
    journal->last = journal->txid;
  clear(journal);
  journal->phase = phase == JOURNAL_DONE ? JOURNAL_IDLE : JOURNAL_STOPPED;
  return 0;
}
