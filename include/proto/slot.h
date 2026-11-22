#ifndef _PROTO_SLOT_H
#define _PROTO_SLOT_H
#include <types.h>

#define PROTO_CONN_MAGIC       UINT64_C(0x4b434f4e4e303031)
#define PROTO_CONN_SLOTS       8
#define PROTO_SLOT_BYTES       4096
#define PROTO_DATA_OFFSET      PROTO_SLOT_BYTES
#define PROTO_CONN_BYTES       (9 * PROTO_SLOT_BYTES)
#define PROTO_DESC_OFFSET      64
#define PROTO_DESC_BYTES       128
#define PROTO_SLOT_FREE        0
#define PROTO_SLOT_SUBMITTED   1
#define PROTO_SLOT_DONE        2
#define PROTO_SLOT_QUARANTINED 3

typedef struct Protohead Protohead;
typedef struct Protoslot Protoslot;
struct Protohead
{
  u64 magic, bytes, connid, serverimage, slots, slotbytes, reserved[2];
};
struct Protoslot
{
  u64 seq, state, payload_bytes, object, span, reserved[3], args[8];
};

STATIC_ASSERT(sizeof(Protohead) == 64, proto_connection_header);
STATIC_ASSERT(sizeof(Protoslot) == PROTO_DESC_BYTES, proto_slot_descriptor);
STATIC_ASSERT(offsetof(Protoslot, span) == 32, proto_slot_span);
STATIC_ASSERT(offsetof(Protoslot, args) == 64, proto_slot_arguments);
STATIC_ASSERT(PROTO_DESC_OFFSET + PROTO_CONN_SLOTS * PROTO_DESC_BYTES <= PROTO_DATA_OFFSET,
              proto_control_page);
#endif
