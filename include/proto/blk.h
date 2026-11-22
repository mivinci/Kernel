#ifndef _PROTO_BLK_H
#define _PROTO_BLK_H
#include <proto/slot.h>

#define BLK_VERSION         UINT64_C(1)
#define BLK_INFO_MAGIC      UINT64_C(0x4b424c4b494e4631)
#define BLK_HELLO           1
#define BLK_INFO            2
#define BLK_READ            3
#define BLK_WRITE           4
#define BLK_FLUSH           5
#define BLK_MAX_IO          PROTO_SLOT_BYTES
#define BLK_INFO_BYTES      96
#define BLK_READONLY        UINT64_C(1)
#define BLK_FLUSHABLE       UINT64_C(2)
#define BLK_WORD(opcode)    ((BLK_VERSION << 32) | (u32) (opcode))
#define BLK_WORD_VERSION(w) ((u32) ((u64) (w) >> 32))
#define BLK_WORD_OPCODE(w)  ((u32) (w))

/* Established connection words: version/opcode, connid, sequence, slot.
   HELLO is the generic no-slot handshake; INFO/READ/WRITE/FLUSH use one
   one-page slot. Fixed INFO output occupies the beginning of that slot. */

/* Slot arguments:
   INFO:  all zero.
   READ:  offset bytes, length bytes, expected geometry generation.
   WRITE: offset bytes, length bytes, expected geometry generation.
   FLUSH: zero, zero, expected geometry generation.
   READ output and WRITE input begin at the slot data offset. */

typedef struct Blkinfo Blkinfo;
struct Blkinfo
{
  u64 magic;
  u64 version;
  u64 device_generation;
  u64 geometry_generation;
  u64 capacity_bytes;
  u64 logical_bytes;
  u64 physical_bytes;
  u64 alignment_bytes;
  u64 flags;
  u64 reserved[3];
};
STATIC_ASSERT(sizeof(Blkinfo) == BLK_INFO_BYTES, blk_info_record);
STATIC_ASSERT(offsetof(Blkinfo, flags) == 64, blk_info_flags);
#endif
