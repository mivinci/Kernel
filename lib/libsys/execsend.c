#include "private/execsend.h"
#include "private/syscall.h"

static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}

isize
sysexecfdbegin(const Execfds *capture, Connwork *work, Conncache *cache, u32 context, u64 owner,
               u64 target_image, u64 deadline, u64 *owned_file, Execsend *send)
{
  Fileslot slot;
  Fileref  pin;
  Opkey   *key;
  u64      address, header, i, continuation[FS_DESC_BYTES / sizeof(u64)];
  isize    count, error;
  if (!capture || !work || !cache || !capture->pool || context > SYSFILE_SIGNAL2 ||
      !separate(send, sizeof(*send), work, sizeof(*work)) ||
      !separate(send, sizeof(*send), cache, sizeof(*cache)) ||
      !separate(send, sizeof(*send), capture, sizeof(*capture)) ||
      !separate(send, sizeof(*send), capture->pool, sizeof(*capture->pool)) ||
      !separate(owned_file, sizeof(*owned_file), send, sizeof(*send)) ||
      !separate(owned_file, sizeof(*owned_file), capture, sizeof(*capture)) ||
      !separate(owned_file, sizeof(*owned_file), capture->pool, sizeof(*capture->pool)))
    return -ERR_INVALID;
  if (capture->self.pid != work->state.self.pid || capture->self.image != work->state.self.image)
    return -ERR_STALE;
  if (send->key.lease.seq)
    return -ERR_BUSY;
  count = sysexecfdmanifest(capture, work->state.peer, target_image, deadline, slot.args,
                            send->rows, BOOT_FD_COUNT);
  if (count <= 0)
    return count < 0 ? count : -ERR_STALE;
  error = sysfilerefview(capture->pool, capture->self, *owned_file, &pin);
  if (error)
    return error;
  if (!pin.pins || pin.connection.index != work->state.tag.index ||
      pin.connection.generation != work->state.tag.generation)
    return -ERR_STATE;
  slot.payload_bytes = (u64) count * sizeof(Fileinherit);
  slot.span = slot.payload_bytes > FS_SLOT_BYTES ? 2 : 1;
  slot.object = slot.reserved[0] = slot.reserved[1] = slot.reserved[2] = 0;
  key = &send->key;
  error = sysopprepare(work, cache, context, (u32) slot.span, owner, owned_file, 0, FS_EXEC_PREPARE,
                       deadline, key);
  if (error)
    return error;
  slot.seq = key->lease.seq;
  slot.state = FS_SLOT_SUBMITTED;
  address = work->state.map.address;
  header = address + FS_DESC_OFFSET + key->lease.slot * FS_DESC_BYTES;
  if (slot.span == 2) {
    for (i = 0; i < ARRAY_SIZE(continuation); i++)
      continuation[i] = 0;
    error = __sys_copy((void *) (header + FS_DESC_BYTES), continuation, sizeof(continuation));
  }
  if (!error)
    error = __sys_copy((void *) (address + FS_DATA_OFFSET + key->lease.slot * FS_SLOT_BYTES),
                       send->rows, slot.payload_bytes);
  if (!error)
    error = __sys_copy((void *) header, &slot, sizeof(slot));
  if (error) {
    if (sysopabort(work, cache, key, owned_file))
      return -ERR_STATE;
    key->lease.seq = 0;
    return error;
  }
  return 0;
}
