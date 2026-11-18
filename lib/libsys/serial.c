#include "private/serial.h"

STATIC_ASSERT(__atomic_always_lock_free(8, 0), serial_lock_free_words);

static isize
check(Serialring *ring, u64 epoch, const void *data, u32 bytes)
{
  u64 base, address;
  if (!ring || !epoch || !IS_ALIGNED((usize) ring, 8) || bytes > SERIAL_BATCH || (bytes && !data))
    return -ERR_INVALID;
  base = (u64) ring;
  address = (u64) data;
  if (base > UINT64_MAX - sizeof(*ring) || address > UINT64_MAX - bytes ||
      (bytes && address < base + sizeof(*ring) && base < address + bytes))
    return -ERR_INVALID;
  if (ring->magic != SERIAL_MAGIC || ring->capacity != SERIAL_CAPACITY || ring->reserved[0] ||
      ring->reserved[1])
    return -ERR_PEER;
  if (ring->epoch != epoch)
    return -ERR_STALE;
  return 0;
}

isize
serialinit(Serialring *ring, u64 epoch)
{
  volatile u8 *memory;
  u32          i;
  if (!ring || !epoch || !IS_ALIGNED((usize) ring, 8) || (u64) ring > UINT64_MAX - sizeof(*ring))
    return -ERR_INVALID;
  memory = (volatile u8 *) ring;
  for (i = 0; i < sizeof(*ring); i++)
    memory[i] = 0;
  ring->capacity = SERIAL_CAPACITY;
  ring->epoch = epoch;
  ring->magic = SERIAL_MAGIC;
  return 0;
}

isize
serialwrite(Serialring *ring, u64 epoch, const void *data, u32 bytes)
{
  const u8 *source = data;
  u64       producer, consumer, flags;
  u32       i, amount;
  isize     error;
  error = check(ring, epoch, data, bytes);
  if (error)
    return error;
  producer = __atomic_load_n(&ring->producer, __ATOMIC_RELAXED);
  consumer = __atomic_load_n(&ring->consumer, __ATOMIC_ACQUIRE);
  flags = __atomic_load_n(&ring->flags, __ATOMIC_ACQUIRE);
  if ((flags & ~(SERIAL_EOF | SERIAL_ERROR)) || consumer > producer ||
      producer - consumer > SERIAL_CAPACITY)
    return -ERR_PEER;
  if (flags)
    return -ERR_STATE;
  amount = (u32) MIN(bytes, SERIAL_CAPACITY - (producer - consumer));
  if (producer > UINT64_MAX - amount)
    return -ERR_OVERFLOW;
  for (i = 0; i < amount; i++)
    ring->data[(producer + i) % SERIAL_CAPACITY] = source[i];
  __atomic_store_n(&ring->producer, producer + amount, __ATOMIC_RELEASE);
  return amount;
}

isize
serialread(Serialring *ring, u64 epoch, void *data, u32 bytes)
{
  u8   *destination = data;
  u64   producer, consumer, flags;
  u32   i, amount;
  isize error;
  error = check(ring, epoch, data, bytes);
  if (error)
    return error;
  consumer = __atomic_load_n(&ring->consumer, __ATOMIC_RELAXED);
  producer = __atomic_load_n(&ring->producer, __ATOMIC_ACQUIRE);
  flags = __atomic_load_n(&ring->flags, __ATOMIC_ACQUIRE);
  if ((flags & ~(SERIAL_EOF | SERIAL_ERROR)) || consumer > producer ||
      producer - consumer > SERIAL_CAPACITY)
    return -ERR_PEER;
  amount = (u32) MIN(bytes, producer - consumer);
  for (i = 0; i < amount; i++)
    destination[i] = ring->data[(consumer + i) % SERIAL_CAPACITY];
  __atomic_store_n(&ring->consumer, consumer + amount, __ATOMIC_RELEASE);
  return amount;
}
