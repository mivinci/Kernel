/* Internal bounded journal, not a userspace ABI. */
#ifndef _KLOG_H
#define _KLOG_H
#include <types.h>
#include <abi/base.h>
#define KLOG_BYTES 65536
#define KLOG_TEXT  256
#define KLOG_INFO  1
#define KLOG_ERROR 2
#define KLOG_TIME  1
typedef struct Klogrec  Klogrec;
typedef struct Klogstat Klogstat;
struct Klogrec
{
  u64 sequence, nanos, source;
  u16 bytes;
  u8  level, flags;
  u32 reserved;
  u8  text[KLOG_TEXT];
};
struct Klogstat
{
  u64 first, last, lost;
  u32 count, capacity;
};
STATIC_ASSERT(sizeof(Klogrec) == 288, kernel_log_record_size);
STATIC_ASSERT(offsetof(Klogrec, text) == 32, kernel_log_header);
/* Single CPU, IRQs masked; valid disjoint caller storage. No allocation or I/O.
   Full ring evicts oldest whole record; loss saturates, sequence never wraps. */
int klogwrite(u64 nanos, u64 source, u32 level, u32 flags, const void *text, u32 bytes);
/* 0 selects oldest; old sequence STALE, empty/future WOULD_BLOCK.
   Failure leaves output unchanged. No userspace read/authorization is added. */
int  klogread(u64 sequence, Klogrec *out);
void klogstat(Klogstat *out);
#endif
