#include <klog.h>
#define KLOG_RECORDS (KLOG_BYTES / sizeof(Klogrec))
#ifndef KLOG_SEQUENCE_MAX
#define KLOG_SEQUENCE_MAX UINT64_MAX
#endif
static struct
{
  Klogrec records[KLOG_RECORDS];
  u8      padding[KLOG_BYTES - KLOG_RECORDS * sizeof(Klogrec)];
} klogstorage;
static u64 last, lost;
static u32 first, count;
STATIC_ASSERT(sizeof(klogstorage) == KLOG_BYTES, kernel_log_storage_size);
static void
loss(void)
{
  if (lost != UINT64_MAX)
    lost++;
}
int
klogwrite(u64 nanos, u64 source, u32 level, u32 flags, const void *text, u32 bytes)
{
  const u8 *input = text;
  Klogrec  *record;
  u32       index, i;
  if (!input || !bytes || bytes > KLOG_TEXT || (level != KLOG_INFO && level != KLOG_ERROR) ||
      flags & ~KLOG_TIME || (!(flags & KLOG_TIME) && nanos))
    return -ERR_INVALID;
  if (last == KLOG_SEQUENCE_MAX) {
    loss();
    return -ERR_OVERFLOW;
  }
  index = (first + count) % KLOG_RECORDS;
  if (count == KLOG_RECORDS) {
    first = (first + 1) % KLOG_RECORDS;
    loss();
  } else
    count++;
  record = &klogstorage.records[index];
  record->sequence = ++last;
  record->nanos = nanos;
  record->source = source;
  record->bytes = (u16) bytes;
  record->level = (u8) level;
  record->flags = (u8) flags;
  record->reserved = 0;
  for (i = 0; i < KLOG_TEXT; i++)
    record->text[i] = i < bytes ? input[i] : 0;
  return 0;
}
int
klogread(u64 sequence, Klogrec *out)
{
  const u8 *input;
  u64       oldest;
  u32       index, i;
  if (!out)
    return -ERR_INVALID;
  if (!count)
    return -ERR_WOULD_BLOCK;
  oldest = last - count + 1;
  if (!sequence)
    sequence = oldest;
  if (sequence < oldest)
    return -ERR_STALE;
  if (sequence > last)
    return -ERR_WOULD_BLOCK;
  index = (first + (u32) (sequence - oldest)) % KLOG_RECORDS;
  input = (const u8 *) &klogstorage.records[index];
  for (i = 0; i < sizeof(*out); i++)
    ((volatile u8 *) out)[i] = input[i];
  return 0;
}
void
klogstat(Klogstat *out)
{
  out->first = count ? last - count + 1 : 0;
  out->last = last;
  out->lost = lost;
  out->count = count;
  out->capacity = KLOG_RECORDS;
}
