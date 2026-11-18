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
/* Reverse hints have meaning at the serial endpoint, not the tty endpoint. */
#define SERIAL_RX_ROOM  (UINT64_C(1) << 32)
#define SERIAL_TX_READY (UINT64_C(1) << 33)

#define SERIAL_VERSION      UINT64_C(1)
#define SERIAL_BIND         1
#define SERIAL_STATUS       2
#define SERIAL_WORD(opcode) ((SERIAL_VERSION << 32) | (u32) (opcode))
#define SERIAL_IRQ_BIT      16
#define SERIAL_IRQ_EVENT    (UINT64_C(1) << SERIAL_IRQ_BIT)

/* Control CALL: version/opcode, nonzero sequence, argument0, argument1.
   BIND: local installed NOTIFY, expected peer image. STATUS: zero, zero.
   Reply: signed native business status, sequence, two operation values. */

/* Serial-private Boot tags; flags/reserved remain zero, values are Handles. */
#define SERIAL_INIT_CALL 0x100
#define SERIAL_MMIO      0x101
#define SERIAL_IRQ_BIND  0x102
#define SERIAL_RX_RING   0x103
#define SERIAL_TX_RING   0x104

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
