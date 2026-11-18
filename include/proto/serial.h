#ifndef _PROTO_SERIAL_H
#define _PROTO_SERIAL_H
#include <types.h>

#define SERIAL_MAGIC      UINT64_C(0x4b5352494e473031)
#define SERIAL_CAPACITY   8192
#define SERIAL_RING_BYTES 16384
#define SERIAL_BATCH      32
#define SERIAL_EOF        UINT64_C(1)
#define SERIAL_ERROR      UINT64_C(2)
#define SERIAL_RX_READY   (UINT64_C(1) << 32)
#define SERIAL_TX_ROOM    (UINT64_C(1) << 33)

typedef struct Serialring Serialring;
struct Serialring
{
  u64 magic, capacity, epoch, producer, consumer, flags, reserved[2];
  u8  data[SERIAL_CAPACITY];
  u8  padding[SERIAL_RING_BYTES - 64 - SERIAL_CAPACITY];
};
STATIC_ASSERT(offsetof(Serialring, producer) == 24, serial_producer_offset);
STATIC_ASSERT(offsetof(Serialring, consumer) == 32, serial_consumer_offset);
STATIC_ASSERT(offsetof(Serialring, flags) == 40, serial_flags_offset);
STATIC_ASSERT(offsetof(Serialring, data) == 64, serial_header_size);
STATIC_ASSERT(sizeof(Serialring) == SERIAL_RING_BYTES, serial_mapping_size);
#endif
